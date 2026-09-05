from __future__ import annotations

import copy
import json
import math
import re
import threading
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any

import yaml
from google.protobuf.descriptor_pb2 import FileDescriptorSet
from google.protobuf.message import DecodeError
from jsonschema import Draft202012Validator
from jsonschema.exceptions import SchemaError, ValidationError

from app.domain.errors import ContractCompatibilityError
from app.domain.models import RuntimeParameterDefinition
from app.domain.scenario_validation import ScenarioValidationResult
from app.domain.telemetry import RuntimeSignalCatalog, RuntimeSignalDefinition
from app.services.scenario_validator import (
    ScenarioValidationUnavailable,
    ScenarioValidator,
)


class RuntimeContractError(ContractCompatibilityError, ValueError):
    """Base class for incompatible or unreadable JSB0 Runtime contracts."""


class RuntimeContractNotFound(RuntimeContractError):
    pass


class UnsupportedRuntimeContractVersion(RuntimeContractError):
    pass


class InvalidRuntimeContract(RuntimeContractError):
    pass


class RuntimeParameterMismatch(RuntimeContractError):
    pass


class RuntimeCapabilityMismatch(RuntimeContractError):
    pass


UnsupportedVersion = UnsupportedRuntimeContractVersion
Invalid = InvalidRuntimeContract
ParameterMismatch = RuntimeParameterMismatch
CapabilityMismatch = RuntimeCapabilityMismatch
RuntimeContractUnsupportedVersion = UnsupportedRuntimeContractVersion
RuntimeContractInvalid = InvalidRuntimeContract
RuntimeContractParameterMismatch = RuntimeParameterMismatch
RuntimeContractCapabilityMismatch = RuntimeCapabilityMismatch


@dataclass(frozen=True)
class HeadlessExecutionCapabilities:
    mode: str
    variants: tuple[str, ...]
    authoritative: bool = True
    modes: tuple[str, ...] = ()
    parameter_overrides: dict[str, Any] | None = None
    embedded_telemetry_descriptor: bool = False
    scenario_types: dict[str, dict[str, Any]] | None = None

    def for_scenario(self, scenario_type: str) -> HeadlessExecutionCapabilities:
        raw = (self.scenario_types or {}).get(scenario_type)
        if raw is None:
            if self.authoritative and self.scenario_types is not None:
                raise RuntimeCapabilityMismatch(
                    f"Runtime does not support scenario_type {scenario_type!r}"
                )
            return self
        if not isinstance(raw, dict):
            raise RuntimeCapabilityMismatch(
                f"Invalid execution capability for scenario_type {scenario_type!r}"
            )
        mode = str(raw.get("mode", "")).strip()
        variants = tuple(
            item for item in raw.get("variants", ()) if isinstance(item, str)
        )
        if (
            not mode
            or not variants
            or (self.modes and mode not in self.modes)
            or any(variant not in self.variants for variant in variants)
        ):
            raise RuntimeCapabilityMismatch(
                f"Invalid execution capability for scenario_type {scenario_type!r}"
            )
        return HeadlessExecutionCapabilities(
            mode=mode,
            variants=variants,
            authoritative=self.authoritative,
            modes=self.modes,
            parameter_overrides=self.parameter_overrides,
            embedded_telemetry_descriptor=self.embedded_telemetry_descriptor,
            scenario_types=self.scenario_types,
        )


@dataclass(frozen=True)
class RuntimeArtifactDefinition:
    type: str
    path: str
    required: bool
    content_type: str
    condition: str | None = None


@dataclass(frozen=True)
class RuntimeArtifactManifest:
    schema_version: int
    artifacts: tuple[RuntimeArtifactDefinition, ...]

    def definition(self, artifact_type: str) -> RuntimeArtifactDefinition:
        try:
            return next(item for item in self.artifacts if item.type == artifact_type)
        except StopIteration as exc:
            raise RuntimeContractNotFound(
                f"JSB0 artifact manifest does not declare {artifact_type}"
            ) from exc

    def path_for(self, artifact_type: str) -> str:
        return self.definition(artifact_type).path


@dataclass(frozen=True)
class RuntimeContractBundle:
    repository_id: int
    commit_sha: str
    runtime_root: Path
    version: str
    index: dict[str, str]
    capabilities: HeadlessExecutionCapabilities
    variants: tuple[str, ...]
    scenario_schema: Draft202012Validator
    parameters: tuple[RuntimeParameterDefinition, ...]
    parameter_set_schema: Draft202012Validator
    artifact_manifest: RuntimeArtifactManifest
    signal_catalog: RuntimeSignalCatalog
    run_schema: Draft202012Validator
    telemetry_descriptor: bytes | None


class LegacyRuntimeContractAdapter:
    """All fixed-path compatibility for pre-index JSB0 revisions lives here."""

    SCENARIO_SCHEMA = Path("contract/scenario/scenario.schema.json")
    EXECUTION_VARIANTS = Path("contract/execution/variants.json")
    EXECUTION_CAPABILITIES = Path("contract/execution/capabilities.json")
    EXECUTION_CAPABILITIES_ALTERNATE = Path("execution/capabilities.json")
    EXECUTION_PARAMETERS = Path("contract/execution/parameters.json")
    PARAMETER_SET_SCHEMA = Path("contract/execution/parameter-set.schema.json")
    ARTIFACTS = Path("contract/execution/artifacts.json")
    TELEMETRY_CATALOG = Path("contract/catalog/signals.yaml")
    RUN_SCHEMA = Path("contract/metadata/run.schema.json")
    CONTRACT_VERSION = Path("contract/VERSION")
    OLD_CONTRACT_VERSION = Path("contract/version.json")

    def path(self, runtime_root: Path, key: str) -> Path:
        mapping = {
            "scenario_schema": self.SCENARIO_SCHEMA,
            "variants": self.EXECUTION_VARIANTS,
            "capabilities": self.EXECUTION_CAPABILITIES,
            "parameters": self.EXECUTION_PARAMETERS,
            "parameter_set_schema": self.PARAMETER_SET_SCHEMA,
            "artifacts": self.ARTIFACTS,
            "signals": self.TELEMETRY_CATALOG,
            "run_schema": self.RUN_SCHEMA,
        }
        return runtime_root / mapping[key]


class RuntimeContractReader:
    """Single owner of JSB0 contract discovery, loading, and validation."""

    INDEX = Path("contract/index.json")
    SUPPORTED_MAJOR_VERSIONS = frozenset({2})
    INDEX_KEYS = frozenset(
        {
            "version",
            "scenario_schema",
            "parameters",
            "parameter_schema",
            "parameter_set_schema",
            "capabilities",
            "variants",
            "artifacts",
            "run_schema",
            "signals",
            "telemetry_descriptor",
        }
    )

    def __init__(self, legacy: LegacyRuntimeContractAdapter | None = None) -> None:
        self.legacy = legacy or LegacyRuntimeContractAdapter()
        self._cache: dict[tuple[int, str], RuntimeContractBundle] = {}
        self._cache_lock = threading.Lock()

    def is_indexed(self, runtime_root: Path) -> bool:
        """Return whether this revision publishes the indexed v2 contract."""
        return (runtime_root / self.INDEX).is_file()

    def load_index(self, runtime_root: Path) -> dict[str, str]:
        path = runtime_root / self.INDEX
        payload = self._read_json(path, "JSB0 contract index")
        if not isinstance(payload, dict):
            raise InvalidRuntimeContract("JSB0 contract index must be an object")
        missing = sorted(self.INDEX_KEYS - set(payload))
        if missing:
            raise InvalidRuntimeContract(
                "JSB0 contract index is missing entries: " + ", ".join(missing)
            )
        result: dict[str, str] = {}
        for key in self.INDEX_KEYS:
            value = payload.get(key)
            if not isinstance(value, str) or not value.strip():
                raise InvalidRuntimeContract(
                    f"JSB0 contract index entry {key} is invalid"
                )
            self._safe_contract_path(runtime_root, value)
            result[key] = value
        return result

    def load_version(self, runtime_root: Path) -> str:
        if self.is_indexed(runtime_root):
            path = self._indexed_path(runtime_root, "version")
        else:
            path = runtime_root / self.legacy.CONTRACT_VERSION
            if not path.is_file():
                old = runtime_root / self.legacy.OLD_CONTRACT_VERSION
                if not old.is_file():
                    raise RuntimeContractNotFound(
                        "JSB0 contract VERSION is unavailable"
                    )
                payload = self._read_json(old, "JSB0 contract version")
                value = payload.get("version") if isinstance(payload, dict) else payload
                version = str(value) if isinstance(value, (str, int)) else ""
                return self._validate_version(version)
        try:
            version = path.read_text(encoding="utf-8").strip()
        except (OSError, UnicodeError) as exc:
            raise InvalidRuntimeContract("JSB0 contract VERSION is invalid") from exc
        return self._validate_version(version)

    def load_capabilities(self, runtime_root: Path) -> HeadlessExecutionCapabilities:
        path = self._contract_path(runtime_root, "capabilities")
        if not path.is_file() and not self.is_indexed(runtime_root):
            alternate = runtime_root / self.legacy.EXECUTION_CAPABILITIES_ALTERNATE
            if alternate.is_file():
                path = alternate
        payload = self._read_json(path, "JSB0 execution capabilities")
        capability = self._headless_capability_values(payload)
        if self.is_indexed(runtime_root):
            declared_major = (
                payload.get("contract_major") if isinstance(payload, dict) else None
            )
            actual_major = int(self.load_version(runtime_root).split(".", 1)[0])
            if declared_major is not None and declared_major != actual_major:
                raise RuntimeCapabilityMismatch(
                    "JSB0 capabilities contract_major does not match VERSION"
                )
        return capability

    def load_variants(self, runtime_root: Path) -> tuple[str, ...]:
        path = self._contract_path(runtime_root, "variants")
        if path.is_file():
            return tuple(
                dict.fromkeys(
                    self._variant_values(
                        self._read_json(path, "JSB0 execution variants")
                    )
                )
            )
        if self.is_indexed(runtime_root):
            raise RuntimeContractNotFound("JSB0 execution variants are unavailable")
        try:
            return self.load_capabilities(runtime_root).variants
        except RuntimeContractError:
            validator = self.load_scenario_schema(runtime_root)
            autopilot = validator.schema.get("properties", {}).get("autopilot", {})
            raw = autopilot.get("enum")
            if not isinstance(raw, list):
                raw = autopilot.get("properties", {}).get("type", {}).get("enum")
            if not isinstance(raw, list):
                raise RuntimeContractNotFound("JSB0 execution variants are unavailable")
            return tuple(item for item in raw if isinstance(item, str) and item)

    def load_parameters(
        self, runtime_root: Path
    ) -> tuple[RuntimeParameterDefinition, ...]:
        path = self._contract_path(runtime_root, "parameters")
        payload = self._read_json(path, "JSB0 execution parameters")
        if not isinstance(payload, dict):
            raise InvalidRuntimeContract("JSB0 execution parameters must be an object")
        if self.is_indexed(runtime_root):
            schema = self._load_schema(runtime_root, "parameter_schema")
            self._validate_instance(schema, payload, "JSB0 execution parameters")
            if payload.get("contract_version") != self.load_version(runtime_root):
                raise RuntimeParameterMismatch(
                    "JSB0 parameter catalog contract_version does not match VERSION"
                )
        raw = payload.get("parameters")
        if not isinstance(raw, list) or not raw:
            raise InvalidRuntimeContract("JSB0 execution parameter catalog is empty")
        try:
            parameters = tuple(
                RuntimeParameterDefinition.model_validate(item) for item in raw
            )
        except (TypeError, ValueError) as exc:
            raise InvalidRuntimeContract(
                "JSB0 execution parameters are invalid"
            ) from exc
        ids = [item.id for item in parameters]
        if len(ids) != len(set(ids)):
            raise InvalidRuntimeContract("JSB0 execution parameter ids must be unique")
        for item in parameters:
            numeric_values = [
                item.minimum,
                item.maximum,
                item.increment,
                item.algorithm_default,
                item.default_value,
                *(profile.get("value") for profile in item.profiles.values()),
            ]
            if any(
                value is not None and not math.isfinite(float(value))
                for value in numeric_values
            ):
                raise InvalidRuntimeContract(
                    f"JSB0 parameter {item.id} contains a non-finite value"
                )
            if (
                item.minimum is not None
                and item.maximum is not None
                and item.minimum > item.maximum
            ):
                raise InvalidRuntimeContract(
                    f"JSB0 parameter {item.id} has invalid bounds"
                )
            if item.minimum is not None and item.default_value < item.minimum:
                raise InvalidRuntimeContract(
                    f"JSB0 parameter {item.id} default is below minimum"
                )
            if item.maximum is not None and item.default_value > item.maximum:
                raise InvalidRuntimeContract(
                    f"JSB0 parameter {item.id} default is above maximum"
                )
        return parameters

    def load_parameter_set_schema(self, runtime_root: Path) -> Draft202012Validator:
        return self._load_schema(runtime_root, "parameter_set_schema")

    def load_scenario_schema(self, runtime_root: Path) -> Draft202012Validator:
        try:
            return self._load_schema(runtime_root, "scenario_schema")
        except RuntimeContractError as exc:
            raise ScenarioValidationUnavailable(str(exc)) from exc

    def load_artifact_manifest(self, runtime_root: Path) -> RuntimeArtifactManifest:
        path = self._contract_path(runtime_root, "artifacts")
        if not path.is_file() and not self.is_indexed(runtime_root):
            return self._legacy_artifact_manifest()
        payload = self._read_json(path, "JSB0 artifact manifest")
        if not isinstance(payload, dict) or not isinstance(
            payload.get("artifacts"), list
        ):
            raise InvalidRuntimeContract(
                "JSB0 artifact manifest must contain artifacts"
            )
        definitions: list[RuntimeArtifactDefinition] = []
        for raw in payload["artifacts"]:
            if not isinstance(raw, dict):
                raise InvalidRuntimeContract("JSB0 artifact entry must be an object")
            try:
                artifact_path = self._safe_relative_path(str(raw["path"]))
                definitions.append(
                    RuntimeArtifactDefinition(
                        type=str(raw["type"]),
                        path=artifact_path,
                        required=bool(raw["required"]),
                        content_type=str(raw["content_type"]),
                        condition=str(raw["condition"])
                        if raw.get("condition") is not None
                        else None,
                    )
                )
            except (KeyError, TypeError, ValueError) as exc:
                raise InvalidRuntimeContract("JSB0 artifact entry is invalid") from exc
        types = [item.type for item in definitions]
        paths = [item.path for item in definitions]
        if len(types) != len(set(types)) or len(paths) != len(set(paths)):
            raise InvalidRuntimeContract("JSB0 artifact types and paths must be unique")
        if self.is_indexed(runtime_root):
            required_types = {
                "run_metadata",
                "scenario_snapshot",
                "telemetry",
                "parameter_set_snapshot",
            }
            missing = sorted(required_types - set(types))
            if missing:
                raise InvalidRuntimeContract(
                    "JSB0 artifact manifest is missing entries: " + ", ".join(missing)
                )
        return RuntimeArtifactManifest(
            int(payload.get("schema_version", 0)), tuple(definitions)
        )

    def legacy_artifact_manifest(self) -> RuntimeArtifactManifest:
        """Artifact defaults for explicitly legacy, non-indexed executions."""
        return self._legacy_artifact_manifest()

    @staticmethod
    def legacy_execution_capabilities() -> HeadlessExecutionCapabilities:
        """Execution shape of the pre-contract bundled runner compatibility path."""
        return HeadlessExecutionCapabilities(
            "compare",
            ("baseline", "primary"),
            authoritative=False,
            modes=("compare",),
        )

    def load_signal_catalog(self, runtime_root: Path) -> RuntimeSignalCatalog:
        path = self._contract_path(runtime_root, "signals")
        try:
            payload = yaml.safe_load(path.read_text(encoding="utf-8"))
        except FileNotFoundError as exc:
            raise RuntimeContractNotFound("JSB0 signal catalog is unavailable") from exc
        except (OSError, UnicodeError, yaml.YAMLError) as exc:
            raise InvalidRuntimeContract("JSB0 signal catalog is invalid") from exc
        if not isinstance(payload, dict) or not isinstance(
            payload.get("signals"), dict
        ):
            raise InvalidRuntimeContract("JSB0 signal catalog must contain signals")
        topics = payload.get("topics", {})
        if not isinstance(topics, dict):
            raise InvalidRuntimeContract("JSB0 signal topics must be an object")
        definitions: list[RuntimeSignalDefinition] = []
        try:
            for signal_id, raw in payload["signals"].items():
                value_range = raw.get("range")
                definitions.append(
                    RuntimeSignalDefinition(
                        id=str(signal_id),
                        topic=str(raw["topic"]),
                        field=str(raw["field"]),
                        type=str(raw["type"]),
                        unit=str(raw["unit"]),
                        frame=str(raw["frame"]),
                        group=str(raw["group"])
                        if raw.get("group") is not None
                        else None,
                        description=str(raw["description"])
                        if raw.get("description") is not None
                        else None,
                        axis=str(raw["axis"]) if raw.get("axis") is not None else None,
                        sign=str(raw["sign"]) if raw.get("sign") is not None else None,
                        convention=str(raw["convention"])
                        if raw.get("convention") is not None
                        else None,
                        value_range=tuple(value_range)
                        if isinstance(value_range, list) and len(value_range) == 2
                        else None,
                        required=bool(raw.get("required", False)),
                    )
                )
        except (KeyError, TypeError, ValueError) as exc:
            raise InvalidRuntimeContract("JSB0 signal definition is invalid") from exc
        if self.is_indexed(runtime_root):
            if str(payload.get("contract_version", "")) != self.load_version(
                runtime_root
            ):
                raise InvalidRuntimeContract(
                    "JSB0 signal catalog contract_version does not match VERSION"
                )
            missing_topics = sorted({item.topic for item in definitions} - set(topics))
            if missing_topics:
                raise InvalidRuntimeContract(
                    "JSB0 signals reference undeclared topics: "
                    + ", ".join(missing_topics)
                )
            signal_ids = [item.id for item in definitions]
            if len(signal_ids) != len(set(signal_ids)):
                raise InvalidRuntimeContract("JSB0 signal ids must be unique")
            if any(
                bound is not None and not math.isfinite(float(bound))
                for item in definitions
                for bound in (item.value_range or ())
            ):
                raise InvalidRuntimeContract(
                    "JSB0 signal catalog contains a non-finite range"
                )
        catalog = RuntimeSignalCatalog(
            contract_version=str(payload.get("contract_version", "")),
            telemetry_schema_version=int(payload.get("telemetry_schema_version", 0)),
            topics=copy.deepcopy(topics),
            signals=tuple(definitions),
        )
        if len(catalog.by_api_id()) != len(definitions):
            raise InvalidRuntimeContract("JSB0 signal ids have ambiguous API names")
        return catalog

    def load_run_schema(self, runtime_root: Path) -> Draft202012Validator:
        return self._load_schema(runtime_root, "run_schema")

    def load_telemetry_descriptor(self, runtime_root: Path) -> bytes:
        if not self.is_indexed(runtime_root):
            raise RuntimeContractNotFound(
                "legacy JSB0 revision has no exported telemetry descriptor"
            )
        path = self._indexed_path(runtime_root, "telemetry_descriptor")
        try:
            payload = path.read_bytes()
        except FileNotFoundError as exc:
            raise RuntimeContractNotFound(
                "JSB0 telemetry descriptor is unavailable"
            ) from exc
        except OSError as exc:
            raise InvalidRuntimeContract(
                "JSB0 telemetry descriptor is unreadable"
            ) from exc
        if not payload:
            raise InvalidRuntimeContract("JSB0 telemetry descriptor is empty")
        try:
            descriptor = FileDescriptorSet.FromString(payload)
        except DecodeError as exc:
            raise InvalidRuntimeContract(
                "JSB0 telemetry descriptor is invalid"
            ) from exc
        if not descriptor.file:
            raise InvalidRuntimeContract(
                "JSB0 telemetry descriptor contains no protobuf files"
            )
        return payload

    def load_bundle(
        self, runtime_root: Path, *, repository_id: int, commit_sha: str
    ) -> RuntimeContractBundle:
        if repository_id < 1 or not re.fullmatch(r"[0-9a-f]{40}", commit_sha):
            raise InvalidRuntimeContract(
                "Runtime contract cache identity requires repository_id and immutable commit SHA"
            )
        key = (repository_id, commit_sha)
        with self._cache_lock:
            cached = self._cache.get(key)
        if cached is not None:
            return cached
        index = self.load_index(runtime_root)
        version = self.load_version(runtime_root)
        capabilities = self.load_capabilities(runtime_root)
        variants = self.load_variants(runtime_root)
        if set(capabilities.variants) - set(variants):
            raise RuntimeCapabilityMismatch(
                "JSB0 capabilities reference variants absent from variants.json"
            )
        parameters = self.load_parameters(runtime_root)
        parameter_variants = {
            variant for parameter in parameters for variant in parameter.variants
        }
        if parameter_variants - set(variants):
            raise RuntimeParameterMismatch(
                "JSB0 parameters reference variants absent from variants.json"
            )
        artifact_manifest = self.load_artifact_manifest(runtime_root)
        overrides = capabilities.parameter_overrides or {}
        override_path = overrides.get("path")
        if (
            overrides.get("supported") is True
            and isinstance(override_path, str)
            and self._safe_relative_path(override_path)
            != artifact_manifest.path_for("parameter_set_snapshot")
        ):
            raise RuntimeCapabilityMismatch(
                "JSB0 parameter override path does not match artifacts.json"
            )
        try:
            telemetry_descriptor = self.load_telemetry_descriptor(runtime_root)
        except RuntimeContractNotFound:
            if not capabilities.embedded_telemetry_descriptor:
                raise
            # Source checkouts may omit the generated export descriptor when
            # the capability guarantees an imports-complete descriptor in
            # each MCAP schema. It is still produced by this exact revision.
            telemetry_descriptor = None
        bundle = RuntimeContractBundle(
            repository_id=repository_id,
            commit_sha=commit_sha,
            runtime_root=runtime_root.resolve(),
            version=version,
            index=index,
            capabilities=capabilities,
            variants=variants,
            scenario_schema=self.load_scenario_schema(runtime_root),
            parameters=parameters,
            parameter_set_schema=self.load_parameter_set_schema(runtime_root),
            artifact_manifest=artifact_manifest,
            signal_catalog=self.load_signal_catalog(runtime_root),
            run_schema=self.load_run_schema(runtime_root),
            telemetry_descriptor=telemetry_descriptor,
        )
        with self._cache_lock:
            existing = self._cache.setdefault(key, bundle)
        return existing

    # Compatibility methods. New consumers use the API above.
    def load_headless_capabilities(
        self, runtime_root: Path
    ) -> HeadlessExecutionCapabilities:
        try:
            return self.load_capabilities(runtime_root)
        except RuntimeContractNotFound:
            variants = self.load_variants(runtime_root)
            return HeadlessExecutionCapabilities(
                "single", variants, authoritative=False
            )

    def load_execution_capabilities(self, runtime_root: Path) -> list[str]:
        return list(self.load_headless_capabilities(runtime_root).variants)

    def load_execution_parameter_contract(
        self, runtime_root: Path
    ) -> dict[str, Any] | None:
        path = self._contract_path(runtime_root, "parameters")
        if not path.is_file() and not self.is_indexed(runtime_root):
            return None
        payload = self._read_json(path, "JSB0 execution parameters")
        if not isinstance(payload, dict):
            raise InvalidRuntimeContract("JSB0 execution parameters must be an object")
        return payload

    def load_telemetry_catalog(self, runtime_root: Path) -> dict[str, Any] | None:
        path = self._contract_path(runtime_root, "signals")
        if not path.is_file() and not self.is_indexed(runtime_root):
            return None
        if not self.is_indexed(runtime_root):
            try:
                payload = yaml.safe_load(path.read_text(encoding="utf-8"))
            except (OSError, UnicodeError, yaml.YAMLError) as exc:
                raise InvalidRuntimeContract("JSB0 signal catalog is invalid") from exc
            if not isinstance(payload, dict):
                raise InvalidRuntimeContract("JSB0 signal catalog must be an object")
            return payload
        catalog = self.load_signal_catalog(runtime_root)
        return {
            "contract_version": catalog.contract_version,
            "telemetry_schema_version": catalog.telemetry_schema_version,
            "topics": copy.deepcopy(catalog.topics),
            "signals": {
                item.id: {
                    "topic": item.topic,
                    "field": item.field,
                    "type": item.type,
                    "unit": item.unit,
                    "frame": item.frame,
                    "group": item.group,
                    "description": item.description,
                    "axis": item.axis,
                    "sign": item.sign,
                    "convention": item.convention,
                    "range": list(item.value_range) if item.value_range else None,
                    "required": item.required,
                }
                for item in catalog.signals
            },
        }

    def load_contract_version(self, runtime_root: Path) -> str | None:
        try:
            return self.load_version(runtime_root)
        except RuntimeContractNotFound:
            return None

    def _contract_path(self, runtime_root: Path, key: str) -> Path:
        if self.is_indexed(runtime_root):
            if key != "version":
                self.load_version(runtime_root)
            return self._indexed_path(runtime_root, key)
        return self.legacy.path(runtime_root, key)

    def _indexed_path(self, runtime_root: Path, key: str) -> Path:
        index = self.load_index(runtime_root)
        return self._safe_contract_path(runtime_root, index[key])

    @staticmethod
    def _safe_contract_path(runtime_root: Path, relative: str) -> Path:
        normalized = PurePosixPath(relative)
        if normalized.is_absolute() or ".." in normalized.parts or "\\" in relative:
            raise InvalidRuntimeContract("JSB0 contract index contains an unsafe path")
        contract_root = (runtime_root / "contract").resolve()
        path = (contract_root / Path(*normalized.parts)).resolve()
        try:
            path.relative_to(contract_root)
        except ValueError as exc:
            raise InvalidRuntimeContract(
                "JSB0 contract index path escapes contract root"
            ) from exc
        return path

    @staticmethod
    def _safe_relative_path(relative: str) -> str:
        normalized = PurePosixPath(relative)
        if (
            not relative.strip()
            or normalized.as_posix() == "."
            or normalized.is_absolute()
            or ".." in normalized.parts
            or "\\" in relative
        ):
            raise InvalidRuntimeContract(
                "JSB0 artifact manifest contains an unsafe path"
            )
        return normalized.as_posix()

    @staticmethod
    def _read_json(path: Path, label: str) -> Any:
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except FileNotFoundError as exc:
            raise RuntimeContractNotFound(f"{label} is unavailable") from exc
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            raise InvalidRuntimeContract(f"{label} is invalid") from exc

    def _load_schema(self, runtime_root: Path, key: str) -> Draft202012Validator:
        payload = self._read_json(self._contract_path(runtime_root, key), f"JSB0 {key}")
        if not isinstance(payload, dict):
            raise InvalidRuntimeContract(f"JSB0 {key} must be an object")
        try:
            Draft202012Validator.check_schema(payload)
        except SchemaError as exc:
            raise InvalidRuntimeContract(
                f"JSB0 {key} is not a valid JSON Schema"
            ) from exc
        return Draft202012Validator(payload)

    @classmethod
    def _validate_version(cls, version: str) -> str:
        match = re.fullmatch(
            r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:[-+][0-9A-Za-z.-]+)?", version
        )
        if match is None:
            raise InvalidRuntimeContract(
                "JSB0 contract VERSION must be semantic version"
            )
        major = int(match.group(1))
        if major not in cls.SUPPORTED_MAJOR_VERSIONS:
            raise UnsupportedRuntimeContractVersion(
                f"Unsupported JSB0 Runtime contract major version: {major}"
            )
        return version

    @staticmethod
    def _validate_instance(
        validator: Draft202012Validator, payload: Any, label: str
    ) -> None:
        try:
            validator.validate(payload)
        except ValidationError as exc:
            path = ".".join(str(item) for item in exc.absolute_path)
            location = f" at {path}" if path else ""
            raise InvalidRuntimeContract(
                f"{label} is invalid{location}: {exc.message}"
            ) from exc

    @staticmethod
    def _variant_values(payload: Any) -> list[str]:
        raw = (
            payload
            if isinstance(payload, list)
            else payload.get("variants")
            if isinstance(payload, dict)
            else None
        )
        if not isinstance(raw, list):
            raise InvalidRuntimeContract(
                "execution variants must contain a variants array"
            )
        result: list[str] = []
        for item in raw:
            value = (
                item
                if isinstance(item, str)
                else item.get("id", item.get("name"))
                if isinstance(item, dict)
                else None
            )
            if not isinstance(value, str) or not value.strip():
                raise InvalidRuntimeContract("execution variant entries require an id")
            result.append(value.strip())
        if not result:
            raise InvalidRuntimeContract(
                "JSB0 Runtime does not declare execution variants"
            )
        return result

    @classmethod
    def _headless_capability_values(cls, payload: Any) -> HeadlessExecutionCapabilities:
        if not isinstance(payload, dict):
            raise InvalidRuntimeContract("execution capabilities must be an object")
        headless = payload.get("headless", payload)
        if not isinstance(headless, dict):
            raise InvalidRuntimeContract("headless capabilities must be an object")
        raw_modes = headless.get("modes")
        modes = (
            tuple(
                item.strip()
                for item in raw_modes
                if isinstance(item, str) and item.strip()
            )
            if isinstance(raw_modes, list)
            else ()
        )
        mode = headless.get("mode", headless.get("headless_mode"))
        if mode is None and len(modes) == 1:
            mode = modes[0]
        if mode is None and "compare_variants" in headless:
            mode = "compare"
        execution = headless.get("execution")
        if mode is None and isinstance(execution, dict):
            mode = execution.get("mode")
        normalized_mode = (
            str(mode).strip().lower().replace("_", "-") if mode is not None else ""
        )
        if normalized_mode in {"compare-only", "comparison"}:
            normalized_mode = "compare"
        variants_payload: Any = headless
        if normalized_mode == "compare" and "compare_variants" in headless:
            variants_payload = {"variants": headless["compare_variants"]}
        elif "variants" not in headless and isinstance(execution, dict):
            variants_payload = execution
        variants = tuple(dict.fromkeys(cls._variant_values(variants_payload)))
        if not normalized_mode:
            raise InvalidRuntimeContract("headless capabilities require a mode")
        if modes and normalized_mode not in modes:
            raise RuntimeCapabilityMismatch(
                "selected execution mode is absent from capabilities modes"
            )
        overrides = headless.get("parameter_overrides")
        telemetry = headless.get("telemetry_contract")
        scenario_types = headless.get("scenario_types")
        return HeadlessExecutionCapabilities(
            normalized_mode,
            variants,
            True,
            modes or (normalized_mode,),
            copy.deepcopy(overrides) if isinstance(overrides, dict) else None,
            bool(telemetry.get("embedded_file_descriptor_set", False))
            if isinstance(telemetry, dict)
            else False,
            copy.deepcopy(scenario_types) if isinstance(scenario_types, dict) else None,
        )

    @staticmethod
    def _legacy_artifact_manifest() -> RuntimeArtifactManifest:
        return RuntimeArtifactManifest(
            0,
            (
                RuntimeArtifactDefinition(
                    "run_metadata", "run.json", True, "application/json"
                ),
                RuntimeArtifactDefinition(
                    "scenario_snapshot", "scenario.yaml", True, "application/yaml"
                ),
                RuntimeArtifactDefinition(
                    "telemetry", "telemetry.mcap", True, "application/x-mcap"
                ),
                RuntimeArtifactDefinition(
                    "parameter_set_snapshot",
                    "parameters.yaml",
                    False,
                    "application/yaml",
                ),
            ),
        )


class RuntimeContractService:
    """Application-facing access to contract-owned validation and capabilities."""

    def __init__(
        self,
        reader: RuntimeContractReader | None = None,
        validator: ScenarioValidator | None = None,
    ) -> None:
        self.reader = reader or RuntimeContractReader()
        self.validator = validator or ScenarioValidator()

    def validate_scenario(
        self,
        content: dict[str, Any],
        runtime_root: Path,
        *,
        runtime_branch: str | None = None,
        runtime_commit: str | None = None,
    ) -> ScenarioValidationResult:
        return self.validator.validate_content(
            content,
            self.validator.load_runtime_contract(runtime_root, reader=self.reader),
            runtime_branch=runtime_branch,
            runtime_commit=runtime_commit,
        )

    def load_execution_capabilities(self, runtime_root: Path) -> list[str]:
        return self.reader.load_execution_capabilities(runtime_root)
