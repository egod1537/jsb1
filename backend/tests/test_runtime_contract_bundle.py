from __future__ import annotations

import json
import subprocess
from pathlib import Path

import pytest
import yaml
from app.config.settings import Settings
from app.domain.repository import RepositoryCreate
from app.main import create_app
from app.services.controller_parameters import (
    ControllerParameterError,
    RuntimeControllerParameterService,
)
from app.services.runtime_contract import (
    InvalidRuntimeContract,
    RuntimeContractNotFound,
    RuntimeContractReader,
    UnsupportedRuntimeContractVersion,
)
from fastapi.testclient import TestClient
from google.protobuf.descriptor_pb2 import FileDescriptorProto, FileDescriptorSet

INDEX = {
    "version": "VERSION",
    "scenario_schema": "schemas/scenario.json",
    "parameters": "execution/parameters.json",
    "parameter_schema": "schemas/parameters.json",
    "parameter_set_schema": "schemas/parameter-set.json",
    "capabilities": "execution/capabilities.json",
    "variants": "execution/variants.json",
    "artifacts": "execution/artifacts.json",
    "run_schema": "schemas/run.json",
    "signals": "catalog/signals.yaml",
    "telemetry_descriptor": "telemetry.pb",
}


def _write_json(root: Path, relative: str, value: object) -> None:
    path = root / "contract" / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def write_indexed_contract(
    root: Path, *, version: str = "2.4.1", default_value: float = 1.0
) -> None:
    contract = root / "contract"
    contract.mkdir(parents=True)
    _write_json(root, "index.json", INDEX)
    (contract / "VERSION").write_text(version + "\n", encoding="utf-8")
    _write_json(
        root,
        "schemas/scenario.json",
        {
            "$schema": "https://json-schema.org/draft/2020-12/schema",
            "type": "object",
            "required": ["controller_parameters"],
            "properties": {
                "controller_parameters": {"type": "array", "items": {"type": "string"}}
            },
        },
    )
    _write_json(
        root,
        "execution/capabilities.json",
        {
            "contract_major": 2,
            "modes": ["compare"],
            "variants": ["reference", "candidate"],
            "compare_variants": ["reference", "candidate"],
            "parameter_overrides": {"supported": True, "path": "tuning/input.yaml"},
        },
    )
    _write_json(
        root, "execution/variants.json", {"variants": ["reference", "candidate"]}
    )
    parameter = {
        "id": "ROLL_GAIN",
        "display_name": "Roll gain",
        "description": "Roll controller gain.",
        "module": "flight.roll",
        "controller": "RollController",
        "type": "number",
        "unit": "ratio",
        "minimum": 0.0,
        "maximum": 2.0,
        "algorithm_default": 0.5,
        "default_value": default_value,
        "increment": 0.1,
        "variants": ["reference"],
        "aircraft": ["test-aircraft"],
        "profiles": {"test-aircraft": {"value": default_value}},
        "read_only": False,
        "experimental": False,
    }
    parameter_document = {
        "contract_version": version,
        "schema_version": 1,
        "parameters": [parameter],
    }
    _write_json(root, "execution/parameters.json", parameter_document)
    _write_json(
        root,
        "schemas/parameters.json",
        {
            "$schema": "https://json-schema.org/draft/2020-12/schema",
            "type": "object",
            "required": ["parameters"],
            "properties": {"parameters": {"type": "array", "minItems": 1}},
        },
    )
    _write_json(
        root,
        "schemas/parameter-set.json",
        {
            "$schema": "https://json-schema.org/draft/2020-12/schema",
            "type": "object",
            "additionalProperties": False,
            "required": ["controller_parameters"],
            "properties": {
                "controller_parameters": {
                    "type": "object",
                    "additionalProperties": False,
                    "properties": {
                        "ROLL_GAIN": {"type": "number", "minimum": 0, "maximum": 2}
                    },
                }
            },
        },
    )
    _write_json(
        root,
        "execution/artifacts.json",
        {
            "schema_version": 1,
            "artifacts": [
                {
                    "type": "run_metadata",
                    "path": "meta/runtime.json",
                    "required": True,
                    "content_type": "application/json",
                },
                {
                    "type": "scenario_snapshot",
                    "path": "inputs/scenario.yml",
                    "required": True,
                    "content_type": "application/yaml",
                },
                {
                    "type": "telemetry",
                    "path": "data/flight.mcap",
                    "required": True,
                    "content_type": "application/x-mcap",
                },
                {
                    "type": "parameter_set_snapshot",
                    "path": "tuning/input.yaml",
                    "required": False,
                    "content_type": "application/yaml",
                },
            ],
        },
    )
    _write_json(
        root,
        "schemas/run.json",
        {
            "$schema": "https://json-schema.org/draft/2020-12/schema",
            "type": "object",
        },
    )
    signal_path = contract / "catalog" / "signals.yaml"
    signal_path.parent.mkdir(parents=True)
    signal_path.write_text(
        yaml.safe_dump(
            {
                "contract_version": version,
                "telemetry_schema_version": 7,
                "topics": {
                    "/jsb/reference/state": {
                        "message": "test.telemetry.State",
                        "source": "reference",
                    }
                },
                "signals": {
                    "aircraft.roll": {
                        "topic": "/jsb/reference/state",
                        "field": "roll_rad",
                        "type": "float64",
                        "unit": "rad",
                        "frame": "body",
                        "axis": "x",
                        "sign": "right-hand positive",
                        "group": "aircraft.attitude",
                        "description": "Measured roll.",
                        "range": [-3.14, 3.14],
                        "required": True,
                    }
                },
            },
            sort_keys=False,
        ),
        encoding="utf-8",
    )
    descriptor = FileDescriptorSet(
        file=[
            FileDescriptorProto(
                name="state.proto", package="test.telemetry", syntax="proto3"
            )
        ]
    )
    (contract / "telemetry.pb").write_bytes(descriptor.SerializeToString())


def test_index_drives_complete_typed_bundle_and_revision_cache(tmp_path: Path) -> None:
    write_indexed_contract(tmp_path)
    reader = RuntimeContractReader()
    commit = "a" * 40

    bundle = reader.load_bundle(tmp_path, repository_id=9, commit_sha=commit)

    assert bundle is reader.load_bundle(tmp_path, repository_id=9, commit_sha=commit)
    assert bundle is not reader.load_bundle(
        tmp_path, repository_id=9, commit_sha="b" * 40
    )
    assert bundle.version == "2.4.1"
    assert bundle.index["scenario_schema"] == "schemas/scenario.json"
    assert bundle.capabilities.mode == "compare"
    assert bundle.variants == ("reference", "candidate")
    assert bundle.parameters[0].module == "flight.roll"
    assert bundle.parameters[0].algorithm_default == 0.5
    assert bundle.artifact_manifest.path_for("telemetry") == "data/flight.mcap"
    signal = bundle.signal_catalog.by_api_id()["roll"]
    assert (signal.topic, signal.field, signal.unit, signal.frame, signal.axis) == (
        "/jsb/reference/state",
        "roll_rad",
        "rad",
        "body",
        "x",
    )
    assert bundle.telemetry_descriptor


def test_contract_version_major_is_rejected_before_semantic_reads(
    tmp_path: Path,
) -> None:
    write_indexed_contract(tmp_path, version="3.0.0")

    with pytest.raises(
        UnsupportedRuntimeContractVersion,
        match="Unsupported JSB0 Runtime contract major version: 3",
    ):
        RuntimeContractReader().load_capabilities(tmp_path)


def test_index_paths_cannot_escape_contract_root(tmp_path: Path) -> None:
    write_indexed_contract(tmp_path)
    index = dict(INDEX, signals="../outside.yaml")
    _write_json(tmp_path, "index.json", index)

    with pytest.raises(InvalidRuntimeContract, match="unsafe path"):
        RuntimeContractReader().load_index(tmp_path)


def test_parameter_schema_and_scenario_whitelist_are_both_enforced(
    tmp_path: Path,
) -> None:
    write_indexed_contract(tmp_path)
    service = RuntimeControllerParameterService(RuntimeContractReader())

    resolved = service.resolve_for_variants(
        tmp_path,
        ["reference", "candidate"],
        {"ROLL_GAIN": 1.5},
        ["ROLL_GAIN"],
    )
    assert resolved.effective == {"ROLL_GAIN": 1.5}
    assert resolved.by_variant == {"reference": {"ROLL_GAIN": 1.5}, "candidate": {}}

    with pytest.raises(ControllerParameterError, match="Invalid JSB0 parameter set"):
        service.resolve_for_variants(
            tmp_path, ["reference"], {"ROLL_GAIN": 3.0}, ["ROLL_GAIN"]
        )
    with pytest.raises(ControllerParameterError, match="UNKNOWN"):
        service.resolve_for_variants(
            tmp_path, ["reference"], {}, ["ROLL_GAIN", "UNKNOWN"]
        )
    with pytest.raises(ControllerParameterError, match="ROLL_GAIN"):
        service.resolve_for_variants(tmp_path, ["reference"], {"ROLL_GAIN": 1.0}, [])


def test_missing_indexed_file_is_not_best_effort(tmp_path: Path) -> None:
    write_indexed_contract(tmp_path)
    (tmp_path / "contract" / "telemetry.pb").unlink()

    with pytest.raises(RuntimeContractNotFound):
        RuntimeContractReader().load_bundle(
            tmp_path, repository_id=1, commit_sha="b" * 40
        )


def test_embedded_descriptor_capability_allows_source_tree_without_export(
    tmp_path: Path,
) -> None:
    write_indexed_contract(tmp_path)
    capability_path = tmp_path / "contract" / "execution" / "capabilities.json"
    capabilities = json.loads(capability_path.read_text(encoding="utf-8"))
    capabilities["telemetry_contract"] = {
        "embedded_file_descriptor_set": True,
    }
    capability_path.write_text(json.dumps(capabilities), encoding="utf-8")
    (tmp_path / "contract" / "telemetry.pb").unlink()

    bundle = RuntimeContractReader().load_bundle(
        tmp_path, repository_id=1, commit_sha="c" * 40
    )

    assert bundle.telemetry_descriptor is None


def test_scenario_to_exact_contract_parameter_and_run_preparation(
    tmp_path: Path,
) -> None:
    data = tmp_path / "data"
    scenarios = tmp_path / "scenarios"
    scenarios.mkdir()
    (scenarios / "roll.yaml").write_text(
        yaml.safe_dump(
            {
                "name": "Contract integration",
                "scenario_type": "roll_hold",
                "autopilot": "candidate",
                "controller_parameters": ["ROLL_GAIN"],
            }
        ),
        encoding="utf-8",
    )
    runtime = data / "repositories" / "jsb0"
    write_indexed_contract(runtime)
    (runtime / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.16)\nproject(jsb0 NONE)\n",
        encoding="utf-8",
    )
    subprocess.run(
        ["git", "init", "-b", "impl"], cwd=runtime, check=True, capture_output=True
    )
    subprocess.run(
        ["git", "config", "user.email", "jsb1@example.test"], cwd=runtime, check=True
    )
    subprocess.run(["git", "config", "user.name", "JSB1 Test"], cwd=runtime, check=True)
    subprocess.run(["git", "add", "."], cwd=runtime, check=True)
    subprocess.run(
        ["git", "commit", "-m", "indexed contract"],
        cwd=runtime,
        check=True,
        capture_output=True,
    )
    commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=runtime,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    settings = Settings(
        data_dir=data,
        database_path=data / "jsb1.db",
        scenario_dir=scenarios,
        bootstrap_runtime_repository=False,
        execution_mode="external",
    )

    with TestClient(create_app(settings)) as client:
        client.app.state.repository_manager.register(
            RepositoryCreate(
                name="jsb0",
                remote_url="local-fixture",
                local_path="jsb0",
                default_branch="impl",
            )
        )
        response = client.post(
            "/api/runs",
            json={
                "scenario": "roll.yaml",
                "branch": "impl",
                "controller_parameters": {"ROLL_GAIN": 1.5},
            },
        )
        assert response.status_code == 202, response.text
        run = client.get(f"/api/runs/{response.json()['id']}").json()["run"]
        assert run["commit_sha"] == commit
        assert run["execution_mode"] == "compare"
        assert run["variants"] == ["reference", "candidate"]
        assert run["controller_parameters"] == {"ROLL_GAIN": 1.5}
        assert run["contract_version"] == "2.4.1"
        assert run["parameter_snapshot_path"].endswith("tuning/input.yaml")
        assert len(run["parameter_snapshot_sha256"]) == 64
        assert not Path(run["scenario_path"]).is_absolute()
        run_root = data / run["output_directory"]
        assert (run_root / "inputs" / "scenario.yml").is_file()
        assert (run_root / "tuning" / "input.yaml").is_file()
