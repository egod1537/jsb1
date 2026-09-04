from __future__ import annotations

import json
from pathlib import Path

import pytest
import yaml
from google.protobuf.descriptor_pb2 import FileDescriptorProto, FileDescriptorSet
from jsb1_analysis.io.bundle import (
    RunBundleError,
    UnsupportedRunBundleContract,
    load_run_bundle,
)
from test_run_loader import write_test_mcap


def _write_bundle(tmp_path: Path, *, version: str = "2.3.0") -> tuple[Path, Path]:
    runtime = tmp_path / "runtime"
    contract = runtime / "contract"
    (contract / "meta").mkdir(parents=True)
    index = {
        "version": "meta/VERSION",
        "variants": "meta/variants.json",
        "artifacts": "meta/artifacts.json",
        "signals": "meta/signals.yaml",
        "telemetry_descriptor": "meta/telemetry.pb",
    }
    (contract / "index.json").write_text(json.dumps(index), encoding="utf-8")
    (contract / "meta/VERSION").write_text(version, encoding="utf-8")
    (contract / "meta/variants.json").write_text(
        json.dumps({"variants": ["primary"]}), encoding="utf-8"
    )
    artifact_paths = {
        "run_metadata": "metadata/execution.json",
        "scenario_snapshot": "inputs/frozen.yaml",
        "parameter_set_snapshot": "inputs/tuning.yaml",
        "telemetry": "outputs/data.mcap",
    }
    (contract / "meta/artifacts.json").write_text(
        json.dumps(
            {
                "artifacts": [
                    {"type": key, "path": value}
                    for key, value in artifact_paths.items()
                ]
            }
        ),
        encoding="utf-8",
    )
    (contract / "meta/signals.yaml").write_text(
        yaml.safe_dump(
            {
                "contract_version": version,
                "telemetry_schema_version": 1,
                "topics": {
                    "roll_cmd": {"message": "numeric", "source": "primary"},
                    "roll": {"message": "numeric", "source": "primary"},
                    "roll_rate": {"message": "numeric", "source": "primary"},
                    "aileron": {"message": "numeric", "source": "primary"},
                },
                "signals": {
                    f"aircraft.{name}": {
                        "topic": "roll_cmd" if name == "commanded_roll" else name,
                        "field": "value",
                        "type": "float64",
                        "unit": "normalized" if name == "aileron" else "rad",
                        "frame": "body",
                    }
                    for name in ("commanded_roll", "roll", "roll_rate", "aileron")
                },
            }
        ),
        encoding="utf-8",
    )
    (contract / "meta/telemetry.pb").write_bytes(
        FileDescriptorSet(
            file=[FileDescriptorProto(name="empty.proto", syntax="proto3")]
        ).SerializeToString()
    )

    run = tmp_path / "run"
    for relative in artifact_paths.values():
        (run / relative).parent.mkdir(parents=True, exist_ok=True)
    (run / artifact_paths["run_metadata"]).write_text(
        json.dumps({"run_id": 17, "contract_version": version}), encoding="utf-8"
    )
    (run / artifact_paths["scenario_snapshot"]).write_text(
        "scenario_type: roll_hold\n", encoding="utf-8"
    )
    (run / artifact_paths["parameter_set_snapshot"]).write_text(
        "controller_parameters: {}\n", encoding="utf-8"
    )
    write_test_mcap(run / artifact_paths["telemetry"])
    return run, runtime


def test_run_bundle_uses_indexed_artifact_layout_and_is_immutable(tmp_path: Path) -> None:
    run, runtime = _write_bundle(tmp_path)
    bundle = load_run_bundle(run, runtime)

    assert bundle.contract_version == "2.3.0"
    assert bundle.variants == ("primary",)
    assert bundle.telemetry_path.name == "data.mcap"
    assert bundle.scenario["scenario_type"] == "roll_hold"
    assert bundle.run_metadata["run_id"] == 17
    with pytest.raises(TypeError):
        bundle.parameters["new"] = 1  # type: ignore[index]


def test_run_bundle_rejects_unsupported_major_and_unsafe_layout(tmp_path: Path) -> None:
    run, runtime = _write_bundle(tmp_path, version="3.0.0")
    with pytest.raises(UnsupportedRunBundleContract):
        load_run_bundle(run, runtime)

    (runtime / "contract/meta/VERSION").write_text("2.0.0", encoding="utf-8")
    manifest = json.loads(
        (runtime / "contract/meta/artifacts.json").read_text(encoding="utf-8")
    )
    manifest["artifacts"][0]["path"] = "../run.json"
    (runtime / "contract/meta/artifacts.json").write_text(
        json.dumps(manifest), encoding="utf-8"
    )
    with pytest.raises(RunBundleError, match="unsafe artifact path"):
        load_run_bundle(run, runtime)


def test_run_bundle_rejects_contract_metadata_from_another_revision(
    tmp_path: Path,
) -> None:
    run, runtime = _write_bundle(tmp_path)
    metadata = run / "metadata/execution.json"
    payload = json.loads(metadata.read_text(encoding="utf-8"))
    payload["contract_version"] = "2.2.0"
    metadata.write_text(json.dumps(payload), encoding="utf-8")

    with pytest.raises(RunBundleError, match="exact Runtime contract"):
        load_run_bundle(run, runtime)
