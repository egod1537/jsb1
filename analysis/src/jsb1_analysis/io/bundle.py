from __future__ import annotations

import json
import re
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from types import MappingProxyType

import yaml
from google.protobuf.descriptor_pb2 import FileDescriptorSet
from google.protobuf.message import DecodeError

from jsb1_analysis.contracts import SignalCatalog, TelemetryContractError
from jsb1_analysis.telemetry import TelemetryDataset

from .mcap import load_dataset


class RunBundleError(ValueError):
    pass


class UnsupportedRunBundleContract(RunBundleError):
    pass


@dataclass(frozen=True)
class ArtifactLayout:
    paths: Mapping[str, str]

    def __post_init__(self) -> None:
        object.__setattr__(self, "paths", MappingProxyType(dict(self.paths)))

    def path_for(self, artifact_type: str) -> str:
        try:
            return self.paths[artifact_type]
        except KeyError as exc:
            raise RunBundleError(
                f"artifact manifest has no {artifact_type!r} entry"
            ) from exc


@dataclass(frozen=True)
class RunBundle:
    root: Path
    contract_root: Path
    contract_version: str
    variants: tuple[str, ...]
    artifact_layout: ArtifactLayout
    run_metadata: Mapping[str, object]
    scenario: Mapping[str, object]
    parameters: Mapping[str, object]
    telemetry_path: Path
    signal_catalog: SignalCatalog
    telemetry_descriptor: bytes

    def __post_init__(self) -> None:
        object.__setattr__(self, "run_metadata", _freeze_mapping(self.run_metadata))
        object.__setattr__(self, "scenario", _freeze_mapping(self.scenario))
        object.__setattr__(self, "parameters", _freeze_mapping(self.parameters))

    def telemetry_dataset(self) -> TelemetryDataset:
        return load_dataset(
            self.telemetry_path,
            signal_catalog=self.signal_catalog,
            descriptor=self.telemetry_descriptor,
            variants=self.variants,
        )


class IndexedRuntimeContract:
    """Read only the index-discovered subset needed by offline analysis."""

    INDEX = Path("contract/index.json")
    REQUIRED_INDEX_KEYS = frozenset(
        {"version", "variants", "artifacts", "signals", "telemetry_descriptor"}
    )

    def __init__(
        self, runtime_root: str | Path, *, supported_majors: tuple[int, ...] = (2,)
    ) -> None:
        self.runtime_root = Path(runtime_root).resolve()
        self.contract_root = (self.runtime_root / "contract").resolve()
        self.supported_majors = supported_majors
        self.index = self._load_json(self.runtime_root / self.INDEX, "contract index")
        missing = self.REQUIRED_INDEX_KEYS - set(self.index)
        if missing:
            raise RunBundleError(
                "contract index is missing entries: " + ", ".join(sorted(missing))
            )

    def path(self, key: str) -> Path:
        raw = self.index.get(key)
        if not isinstance(raw, str) or not raw:
            raise RunBundleError(f"contract index entry {key!r} is invalid")
        candidate = (self.contract_root / raw).resolve()
        if candidate != self.contract_root and self.contract_root not in candidate.parents:
            raise RunBundleError(f"contract index entry {key!r} escapes contract root")
        return candidate

    def version(self) -> str:
        try:
            value = self.path("version").read_text(encoding="utf-8").strip()
        except OSError as exc:
            raise RunBundleError("could not read contract version") from exc
        match = re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", value)
        if match is None:
            raise RunBundleError(f"invalid contract version: {value!r}")
        major = int(match.group(1))
        if major not in self.supported_majors:
            raise UnsupportedRunBundleContract(
                f"unsupported runtime contract major version: {major}"
            )
        return value

    def variants(self) -> tuple[str, ...]:
        payload = self._load_json(self.path("variants"), "execution variants")
        raw = payload.get("variants")
        if not isinstance(raw, list) or not all(
            isinstance(item, str) and item for item in raw
        ):
            raise RunBundleError("execution variants are invalid")
        return tuple(dict.fromkeys(raw))

    def artifacts(self) -> ArtifactLayout:
        payload = self._load_json(self.path("artifacts"), "artifact manifest")
        raw = payload.get("artifacts")
        if not isinstance(raw, list):
            raise RunBundleError("artifact manifest is invalid")
        paths: dict[str, str] = {}
        for item in raw:
            if not isinstance(item, Mapping):
                raise RunBundleError("artifact manifest entry is invalid")
            artifact_type = item.get("type")
            relative = item.get("path")
            if not isinstance(artifact_type, str) or not isinstance(relative, str):
                raise RunBundleError("artifact manifest entry is invalid")
            _safe_artifact_path(relative)
            if artifact_type in paths:
                raise RunBundleError(f"duplicate artifact type: {artifact_type}")
            paths[artifact_type] = relative
        return ArtifactLayout(paths)

    @staticmethod
    def _load_json(path: Path, label: str) -> dict[str, object]:
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            raise RunBundleError(f"could not read {label}") from exc
        if not isinstance(payload, dict):
            raise RunBundleError(f"{label} must be an object")
        return payload


def load_run_bundle(
    run_root: str | Path,
    runtime_root: str | Path,
    *,
    supported_contract_majors: tuple[int, ...] = (2,),
) -> RunBundle:
    root = Path(run_root).resolve()
    if not root.is_dir():
        raise RunBundleError(f"run bundle directory not found: {root}")
    contract = IndexedRuntimeContract(
        runtime_root, supported_majors=supported_contract_majors
    )
    version = contract.version()
    layout = contract.artifacts()
    run_metadata = _load_mapping(
        _artifact_path(root, layout.path_for("run_metadata")), "run metadata", json_only=True
    )
    declared_contract = run_metadata.get("contract_version")
    if declared_contract is not None and declared_contract != version:
        raise RunBundleError(
            "run metadata contract_version does not match exact Runtime contract"
        )
    scenario = _load_mapping(
        _artifact_path(root, layout.path_for("scenario_snapshot")), "scenario snapshot"
    )
    parameter_path = _artifact_path(root, layout.path_for("parameter_set_snapshot"))
    parameters = (
        _load_mapping(parameter_path, "parameter snapshot")
        if parameter_path.is_file()
        else {}
    )
    telemetry_path = _artifact_path(root, layout.path_for("telemetry"))
    if not telemetry_path.is_file():
        raise RunBundleError("telemetry artifact is missing")
    try:
        signal_catalog = SignalCatalog.load(contract.path("signals"))
    except TelemetryContractError as exc:
        raise RunBundleError(str(exc)) from exc
    if signal_catalog.contract_version != version:
        raise RunBundleError("signal catalog contract_version does not match VERSION")
    try:
        descriptor = contract.path("telemetry_descriptor").read_bytes()
    except OSError as exc:
        raise RunBundleError("telemetry descriptor is missing") from exc
    if not descriptor:
        raise RunBundleError("telemetry descriptor is empty")
    try:
        descriptor_set = FileDescriptorSet.FromString(descriptor)
    except DecodeError as exc:
        raise RunBundleError("telemetry descriptor is invalid") from exc
    if not descriptor_set.file:
        raise RunBundleError("telemetry descriptor contains no files")
    return RunBundle(
        root=root,
        contract_root=contract.contract_root,
        contract_version=version,
        variants=contract.variants(),
        artifact_layout=layout,
        run_metadata=run_metadata,
        scenario=scenario,
        parameters=parameters,
        telemetry_path=telemetry_path,
        signal_catalog=signal_catalog,
        telemetry_descriptor=descriptor,
    )


def _safe_artifact_path(relative: str) -> Path:
    path = Path(relative)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise RunBundleError(f"unsafe artifact path: {relative!r}")
    return path


def _artifact_path(root: Path, relative: str) -> Path:
    candidate = (root / _safe_artifact_path(relative)).resolve()
    if root not in candidate.parents:
        raise RunBundleError(f"artifact path escapes run root: {relative!r}")
    return candidate


def _load_mapping(path: Path, label: str, *, json_only: bool = False) -> dict[str, object]:
    try:
        text = path.read_text(encoding="utf-8")
        payload = json.loads(text) if json_only else yaml.safe_load(text)
    except (OSError, UnicodeError, json.JSONDecodeError, yaml.YAMLError) as exc:
        raise RunBundleError(f"could not read {label}") from exc
    if not isinstance(payload, dict):
        raise RunBundleError(f"{label} must be an object")
    return payload


def _freeze_mapping(value: Mapping[str, object]) -> Mapping[str, object]:
    return MappingProxyType({key: _freeze(item) for key, item in value.items()})


def _freeze(value: object) -> object:
    if isinstance(value, Mapping):
        return _freeze_mapping(value)
    if isinstance(value, list):
        return tuple(_freeze(item) for item in value)
    return value
