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
    RuntimeCapabilityMismatch,
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


def test_exact_revision_exposes_course_pitch_and_tecs_capability_and_parameter_metadata(
    tmp_path: Path,
) -> None:
    write_indexed_contract(tmp_path)
    capability_path = tmp_path / "contract" / "execution" / "capabilities.json"
    capabilities = json.loads(capability_path.read_text(encoding="utf-8"))
    capabilities["modes"] = ["compare", "single"]
    capabilities["scenario_types"] = {
        "roll_hold": {"mode": "compare", "variants": ["reference", "candidate"]},
        "course_hold": {
            "mode": "single",
            "variants": ["reference"],
            "primary_supported": False,
            "reason": "candidate has no Course Hold controller",
        },
        "pitch_hold": {
            "mode": "single",
            "variants": ["reference"],
            "primary_supported": False,
            "reason": "candidate has no Pitch Hold controller",
        },
        "tecs": {
            "mode": "single",
            "variants": ["reference"],
            "primary_supported": False,
            "reason": "candidate has no TECS controller",
        },
    }
    capability_path.write_text(json.dumps(capabilities), encoding="utf-8")

    parameter_path = tmp_path / "contract" / "execution" / "parameters.json"
    parameters = json.loads(parameter_path.read_text(encoding="utf-8"))
    parameters["parameters"][0]["scenario_types"] = ["roll_hold"]
    parameters["parameters"].append(
        {
            **parameters["parameters"][0],
            "id": "NPFG_PERIOD",
            "display_name": "Lateral guidance response period",
            "description": "Period that controls the lateral guidance response.",
            "module": "px4.course",
            "controller": "Px4CourseController",
            "group": "PX4 Course / Lateral Guidance",
            "unit": "s",
            "minimum": 1.0,
            "maximum": 100.0,
            "algorithm_default": 10.0,
            "default_value": 10.0,
            "increment": 0.1,
            "variants": ["reference"],
            "scenario_types": ["course_hold"],
            "profiles": {"test-aircraft": {"value": 10.0}},
        }
    )
    pitch_metadata = (
        ("FW_P_TC", "s", 0.2, 1.0, 0.2, 0.05),
        ("FW_P_RMAX_POS", "deg/s", 0.0, 180.0, 14.0, 0.5),
        ("FW_P_RMAX_NEG", "deg/s", 0.0, 180.0, 10.0, 0.5),
        ("FW_PR_P", "%/rad/s", 0.0, 10.0, 4.5, 0.005),
        ("FW_PR_I", "%/rad", 0.0, 10.0, 4.5, 0.005),
        ("FW_PR_D", "%/rad/s", 0.0, 10.0, 0.0, 0.005),
        ("FW_PR_FF", "%/rad/s", -10.0, 10.0, 1.2, 0.05),
        ("FW_PR_IMAX", "normalized", 0.0, 1.0, 0.4, 0.05),
    )
    for identifier, unit, minimum, maximum, default, increment in pitch_metadata:
        parameters["parameters"].append(
            {
                **parameters["parameters"][0],
                "id": identifier,
                "display_name": identifier,
                "description": f"Exact {identifier} metadata.",
                "module": "px4.pitch",
                "controller": "Px4PitchController",
                "group": "PX4 Pitch",
                "unit": unit,
                "minimum": minimum,
                "maximum": maximum,
                "algorithm_default": default,
                "default_value": default,
                "increment": increment,
                "variants": ["reference"],
                "scenario_types": ["pitch_hold"],
                "profiles": {"test-aircraft": {"value": default}},
            }
        )
    tecs_ids = (
        "FW_P_LIM_MIN", "FW_P_LIM_MAX", "FW_THR_MIN", "FW_THR_MAX",
        "FW_THR_TRIM", "FW_AIRSPD_MIN", "FW_AIRSPD_MAX", "FW_T_CLMB_MAX",
        "FW_T_SINK_MAX", "FW_T_HRATE_P", "FW_T_SPDWEIGHT_P",
        "FW_T_THR_DAMP", "FW_T_I_GAIN_THR", "FW_T_PTCH_DAMP",
        "FW_T_I_GAIN_PIT", "FW_T_SEB_R_FF", "FW_T_STE_R_TC",
        "FW_T_PITCH_RATE", "FW_T_THR_SLEW",
    )
    for identifier in tecs_ids:
        is_angle = identifier in {"FW_P_LIM_MIN", "FW_P_LIM_MAX", "FW_T_PITCH_RATE"}
        parameters["parameters"].append(
            {
                **parameters["parameters"][0],
                "id": identifier,
                "display_name": identifier,
                "description": f"Exact {identifier} metadata.",
                "module": "px4.tecs",
                "controller": "Px4TecsController",
                "group": "PX4 TECS / Energy Loop",
                "unit": "rad" if is_angle else "ratio",
                "display_unit": "deg" if is_angle else "ratio",
                "display_scale": 57.29577951308232 if is_angle else 1.0,
                "minimum": 0.0,
                "maximum": 10.0,
                "algorithm_default": 1.0,
                "default_value": 1.0,
                "increment": 0.1,
                "variants": ["reference"],
                "scenario_types": ["tecs"],
                "profiles": {"test-aircraft": {"value": 1.0}},
            }
        )
    parameter_path.write_text(json.dumps(parameters), encoding="utf-8")
    parameter_set_path = tmp_path / "contract" / "schemas" / "parameter-set.json"
    parameter_schema = json.loads(parameter_set_path.read_text(encoding="utf-8"))
    parameter_schema["properties"]["controller_parameters"]["properties"][
        "NPFG_PERIOD"
    ] = {"type": "number", "minimum": 1, "maximum": 100}
    for identifier, _unit, minimum, maximum, _default, _increment in pitch_metadata:
        parameter_schema["properties"]["controller_parameters"]["properties"][
            identifier
        ] = {"type": "number", "minimum": minimum, "maximum": maximum}
    for identifier in tecs_ids:
        parameter_schema["properties"]["controller_parameters"]["properties"][
            identifier
        ] = {"type": "number", "minimum": 0.0, "maximum": 10.0}
    parameter_set_path.write_text(json.dumps(parameter_schema), encoding="utf-8")

    reader = RuntimeContractReader()
    bundle = reader.load_bundle(
        tmp_path, repository_id=7, commit_sha="d" * 40
    )
    course = bundle.capabilities.for_scenario("course_hold")
    assert course.mode == "single"
    assert course.variants == ("reference",)
    definition = next(item for item in bundle.parameters if item.id == "NPFG_PERIOD")
    assert definition.module == "px4.course"
    assert definition.group == "PX4 Course / Lateral Guidance"
    assert definition.minimum == 1.0
    assert definition.maximum == 100.0
    assert definition.default_value == 10.0
    assert definition.unit == "s"
    assert definition.scenario_types == ["course_hold"]
    pitch = bundle.capabilities.for_scenario("pitch_hold")
    assert pitch.mode == "single"
    assert pitch.variants == ("reference",)
    pitch_definitions = [
        item for item in bundle.parameters if item.scenario_types == ["pitch_hold"]
    ]
    assert [item.id for item in pitch_definitions] == [
        item[0] for item in pitch_metadata
    ]
    assert all(item.module == "px4.pitch" and item.group == "PX4 Pitch" for item in pitch_definitions)
    tecs = bundle.capabilities.for_scenario("tecs")
    assert tecs.mode == "single"
    assert tecs.variants == ("reference",)
    tecs_definitions = [
        item for item in bundle.parameters if item.scenario_types == ["tecs"]
    ]
    assert [item.id for item in tecs_definitions] == list(tecs_ids)
    assert all(item.module == "px4.tecs" for item in tecs_definitions)
    pitch_limit = next(item for item in tecs_definitions if item.id == "FW_P_LIM_MAX")
    assert pitch_limit.unit == "rad"
    assert pitch_limit.display_unit == "deg"
    assert pitch_limit.display_scale == pytest.approx(57.29577951308232)

    service = RuntimeControllerParameterService(reader)
    resolved = service.resolve_for_variants(
        tmp_path,
        ["reference"],
        {"NPFG_PERIOD": 12.5},
        ["NPFG_PERIOD"],
        "course_hold",
    )
    assert resolved.effective == {"NPFG_PERIOD": 12.5}
    pitch_resolved = service.resolve_for_variants(
        tmp_path,
        ["reference"],
        {"FW_P_TC": 0.3},
        ["FW_P_TC"],
        "pitch_hold",
    )
    assert pitch_resolved.effective == {"FW_P_TC": 0.3}
    tecs_resolved = service.resolve_for_variants(
        tmp_path,
        ["reference"],
        {"FW_T_HRATE_P": 1.5},
        ["FW_T_HRATE_P"],
        "tecs",
    )
    assert tecs_resolved.effective == {"FW_T_HRATE_P": 1.5}
    with pytest.raises(ControllerParameterError, match="ROLL_GAIN"):
        service.resolve_for_variants(
            tmp_path,
            ["reference"],
            {"ROLL_GAIN": 1.0},
            ["ROLL_GAIN"],
            "course_hold",
        )


def test_authoritative_older_revision_reports_tecs_as_unsupported(
    tmp_path: Path,
) -> None:
    write_indexed_contract(tmp_path)
    capability_path = tmp_path / "contract" / "execution" / "capabilities.json"
    capabilities = json.loads(capability_path.read_text(encoding="utf-8"))
    capabilities["scenario_types"] = {
        "roll_hold": {"mode": "compare", "variants": ["reference", "candidate"]}
    }
    capability_path.write_text(json.dumps(capabilities), encoding="utf-8")

    bundle = RuntimeContractReader().load_bundle(
        tmp_path, repository_id=8, commit_sha="e" * 40
    )
    with pytest.raises(RuntimeCapabilityMismatch, match="tecs"):
        bundle.capabilities.for_scenario("tecs")


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
    (scenarios / "course.yaml").write_text(
        yaml.safe_dump(
            {
                "name": "Course contract integration",
                "scenario_type": "course_hold",
                "controller_parameters": ["NPFG_PERIOD"],
            }
        ),
        encoding="utf-8",
    )
    (scenarios / "pitch.yaml").write_text(
        yaml.safe_dump(
            {
                "name": "Pitch contract integration",
                "scenario_type": "pitch_hold",
                "controller_parameters": ["FW_P_TC"],
            }
        ),
        encoding="utf-8",
    )
    runtime = data / "repositories" / "jsb0"
    write_indexed_contract(runtime)
    capability_path = runtime / "contract" / "execution" / "capabilities.json"
    capabilities = json.loads(capability_path.read_text(encoding="utf-8"))
    capabilities["modes"] = ["compare", "single"]
    capabilities["scenario_types"] = {
        "roll_hold": {"mode": "compare", "variants": ["reference", "candidate"]},
        "course_hold": {"mode": "single", "variants": ["reference"]},
        "pitch_hold": {"mode": "single", "variants": ["reference"]},
    }
    capability_path.write_text(json.dumps(capabilities), encoding="utf-8")
    parameter_path = runtime / "contract" / "execution" / "parameters.json"
    parameters = json.loads(parameter_path.read_text(encoding="utf-8"))
    parameters["parameters"].append(
        {
            **parameters["parameters"][0],
            "id": "NPFG_PERIOD",
            "display_name": "Lateral guidance response period",
            "module": "px4.course",
            "group": "PX4 Course / Lateral Guidance",
            "unit": "s",
            "minimum": 1.0,
            "maximum": 100.0,
            "algorithm_default": 10.0,
            "default_value": 10.0,
            "increment": 0.1,
            "variants": ["reference"],
            "scenario_types": ["course_hold"],
            "profiles": {"test-aircraft": {"value": 10.0}},
        }
    )
    parameters["parameters"].append(
        {
            **parameters["parameters"][0],
            "id": "FW_P_TC",
            "display_name": "Pitch time constant",
            "description": "Pitch attitude time constant.",
            "module": "px4.pitch",
            "controller": "Px4PitchController",
            "group": "PX4 Pitch",
            "unit": "s",
            "minimum": 0.2,
            "maximum": 1.0,
            "algorithm_default": 0.2,
            "default_value": 0.2,
            "increment": 0.05,
            "variants": ["reference"],
            "scenario_types": ["pitch_hold"],
            "profiles": {"test-aircraft": {"value": 0.2}},
        }
    )
    parameter_path.write_text(json.dumps(parameters), encoding="utf-8")
    parameter_set_path = runtime / "contract" / "schemas" / "parameter-set.json"
    parameter_schema = json.loads(parameter_set_path.read_text(encoding="utf-8"))
    parameter_schema["properties"]["controller_parameters"]["properties"][
        "NPFG_PERIOD"
    ] = {"type": "number", "minimum": 1, "maximum": 100}
    parameter_schema["properties"]["controller_parameters"]["properties"][
        "FW_P_TC"
    ] = {"type": "number", "minimum": 0.2, "maximum": 1.0}
    parameter_set_path.write_text(json.dumps(parameter_schema), encoding="utf-8")
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

        course_response = client.post(
            "/api/runs",
            json={
                "scenario": "course.yaml",
                "branch": "impl",
                "controller_parameters": {"NPFG_PERIOD": 12.0},
            },
        )
        assert course_response.status_code == 202, course_response.text
        course_run = client.get(
            f"/api/runs/{course_response.json()['id']}"
        ).json()["run"]
        assert course_run["scenario_type"] == "course_hold"
        assert course_run["execution_mode"] == "single"
        assert course_run["variants"] == ["reference"]
        assert course_run["controller_parameters"] == {"NPFG_PERIOD": 12.0}

        pitch_response = client.post(
            "/api/runs",
            json={
                "scenario": "pitch.yaml",
                "branch": "impl",
                "controller_parameters": {"FW_P_TC": 0.3},
            },
        )
        assert pitch_response.status_code == 202, pitch_response.text
        pitch_run = client.get(
            f"/api/runs/{pitch_response.json()['id']}"
        ).json()["run"]
        assert pitch_run["scenario_type"] == "pitch_hold"
        assert pitch_run["execution_mode"] == "single"
        assert pitch_run["variants"] == ["reference"]
        assert pitch_run["controller_parameters"] == {"FW_P_TC": 0.3}
