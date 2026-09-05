from __future__ import annotations

import io
import json
from pathlib import Path

import yaml

from app.services.scenario_validation import main, validate_directory


def _write_runtime_contract(runtime_root: Path) -> None:
    schema_dir = runtime_root / "contract" / "scenario"
    schema_dir.mkdir(parents=True)
    (schema_dir / "scenario.schema.json").write_text(
        json.dumps(
            {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "additionalProperties": False,
                "required": [
                    "schema_version",
                    "scenario_type",
                    "name",
                    "aircraft",
                    "simulation",
                    "events",
                ],
                "properties": {
                    "schema_version": {"const": 1},
                    "scenario_type": {"const": "roll_hold"},
                    "name": {"type": "string", "minLength": 1},
                    "aircraft": {"const": "c172x"},
                    "simulation": {
                        "type": "object",
                        "additionalProperties": False,
                        "required": ["duration_sec"],
                        "properties": {
                            "duration_sec": {
                                "type": "number",
                                "exclusiveMinimum": 0,
                            }
                        },
                    },
                    "events": {"type": "array", "minItems": 1},
                },
            }
        ),
        encoding="utf-8",
    )


def _valid_scenario() -> str:
    return (
        "schema_version: 1\n"
        "scenario_type: roll_hold\n"
        "name: Roll hold\n"
        "aircraft: c172x\n"
        "simulation:\n"
        "  duration_sec: 1\n"
        "events:\n"
        "  - time_sec: 0.5\n"
    )


def test_validator_discovers_nested_yaml_and_yml_files(tmp_path: Path) -> None:
    scenario_dir = tmp_path / "scenarios"
    (scenario_dir / "samples" / "nested").mkdir(parents=True)
    (scenario_dir / "smoke").mkdir()
    (scenario_dir / "samples" / "nested" / "sample.yaml").write_text(
        _valid_scenario(), encoding="utf-8"
    )
    (scenario_dir / "smoke" / "fixture.yml").write_text(
        _valid_scenario(), encoding="utf-8"
    )
    runtime_root = tmp_path / "jsb0"
    _write_runtime_contract(runtime_root)
    output = io.StringIO()

    summary = validate_directory(scenario_dir, runtime_root, output)

    assert summary.validated == 2
    assert summary.passed == 2
    assert summary.failed == 0
    assert summary.exit_code == 0
    assert "PASS" in output.getvalue()
    assert "all bundled scenarios compatible" in output.getvalue()


def test_validator_aggregates_yaml_and_contract_failures(tmp_path: Path) -> None:
    scenario_dir = tmp_path / "scenarios"
    scenario_dir.mkdir()
    (scenario_dir / "invalid-syntax.yaml").write_text(
        "name: [unterminated\n", encoding="utf-8"
    )
    (scenario_dir / "missing-scenario-type.yaml").write_text(
        _valid_scenario().replace("scenario_type: roll_hold\n", ""),
        encoding="utf-8",
    )
    (scenario_dir / "legacy-autopilot.yaml").write_text(
        _valid_scenario() + "autopilot: baseline\n", encoding="utf-8"
    )
    (scenario_dir / "extra-property.yml").write_text(
        _valid_scenario() + "unexpected: true\n", encoding="utf-8"
    )
    runtime_root = tmp_path / "jsb0"
    _write_runtime_contract(runtime_root)
    output = io.StringIO()

    summary = validate_directory(scenario_dir, runtime_root, output)

    report = output.getvalue()
    assert summary.validated == 4
    assert summary.passed == 0
    assert summary.failed == 4
    assert summary.exit_code == 1
    assert report.count("FAIL ") == 4
    assert "'scenario_type' is a required property" in report
    assert "autopilot" in report
    assert "Additional properties are not allowed" in report
    assert "Failed: 4" in report


def test_validator_reports_missing_required_contract_field(tmp_path: Path) -> None:
    scenario_dir = tmp_path / "scenarios"
    scenario_dir.mkdir()
    (scenario_dir / "missing-simulation.yaml").write_text(
        _valid_scenario().replace("simulation:\n  duration_sec: 1\n", ""),
        encoding="utf-8",
    )
    runtime_root = tmp_path / "jsb0"
    _write_runtime_contract(runtime_root)
    output = io.StringIO()

    summary = validate_directory(scenario_dir, runtime_root, output)

    assert summary.failed == 1
    assert "'simulation' is a required property" in output.getvalue()


def test_cli_returns_one_when_any_scenario_fails(tmp_path: Path) -> None:
    scenario_dir = tmp_path / "scenarios"
    scenario_dir.mkdir()
    (scenario_dir / "invalid.yaml").write_text(
        _valid_scenario() + "unexpected: true\n", encoding="utf-8"
    )
    runtime_root = tmp_path / "jsb0"
    _write_runtime_contract(runtime_root)

    exit_code = main(
        [
            "--scenario-dir",
            str(scenario_dir),
            "--runtime-root",
            str(runtime_root),
        ],
        output=io.StringIO(),
    )

    assert exit_code == 1


def test_validator_accepts_discriminated_course_hold_and_rejects_roll_command(
    tmp_path: Path,
) -> None:
    scenario_dir = tmp_path / "scenarios"
    scenario_dir.mkdir()
    course = (
        "schema_version: 1\n"
        "scenario_type: course_hold\n"
        "name: Course hold\n"
        "aircraft: c172x\n"
        "simulation:\n"
        "  duration_sec: 1\n"
        "events:\n"
        "  - time_sec: 0.5\n"
        "    command:\n"
        "      type: course_hold\n"
        "      course_deg: 10\n"
    )
    (scenario_dir / "course.yaml").write_text(course, encoding="utf-8")
    runtime_root = tmp_path / "jsb0"
    schema_dir = runtime_root / "contract" / "scenario"
    schema_dir.mkdir(parents=True)
    (schema_dir / "scenario.schema.json").write_text(
        json.dumps(
            {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "$defs": {
                    "common": {
                        "type": "object",
                        "required": [
                            "schema_version",
                            "scenario_type",
                            "name",
                            "aircraft",
                            "simulation",
                            "events",
                        ],
                        "properties": {
                            "schema_version": {"const": 1},
                            "name": {"type": "string"},
                            "aircraft": {"const": "c172x"},
                            "simulation": {"type": "object"},
                            "events": {"type": "array", "minItems": 1},
                        },
                    },
                    "course": {
                        "properties": {
                            "scenario_type": {"const": "course_hold"},
                            "events": {
                                "items": {
                                    "properties": {
                                        "command": {
                                            "type": "object",
                                            "required": ["type", "course_deg"],
                                            "properties": {
                                                "type": {"const": "course_hold"},
                                                "course_deg": {
                                                    "type": "number",
                                                    "minimum": 0,
                                                    "maximum": 360,
                                                },
                                            },
                                        }
                                    }
                                }
                            },
                        }
                    },
                },
                "allOf": [{"$ref": "#/$defs/common"}],
                "oneOf": [{"$ref": "#/$defs/course"}],
            }
        ),
        encoding="utf-8",
    )

    valid = validate_directory(scenario_dir, runtime_root, io.StringIO())
    assert valid.passed == 1

    (scenario_dir / "course.yaml").write_text(
        course.replace("type: course_hold\n      course_deg: 10", "type: roll_hold\n      roll_deg: 10"),
        encoding="utf-8",
    )
    output = io.StringIO()
    invalid = validate_directory(scenario_dir, runtime_root, output)
    assert invalid.failed == 1
    assert "course_hold" in output.getvalue() or "course_deg" in output.getvalue()
def test_validator_accepts_pitch_hold_and_rejects_out_of_range_command(
    tmp_path: Path,
) -> None:
    scenario_dir = tmp_path / "scenarios"
    scenario_dir.mkdir()
    pitch = {
        "schema_version": 1,
        "scenario_type": "pitch_hold",
        "name": "Pitch hold",
        "aircraft": "c172x",
        "events": [
            {
                "time_sec": 0.5,
                "command": {"type": "pitch_hold", "pitch_deg": 5},
            }
        ],
        "acceptance": {
            "settling_band_deg": 0.2,
            "settling_time_limit_sec": 3,
            "overshoot_limit_deg": 0.5,
            "max_oscillation_cycles": 1,
            "steady_state_error_limit_deg": 0.1,
            "steady_state_rate_limit_deg_s": 0.1,
        },
    }
    path = scenario_dir / "pitch.yaml"
    path.write_text(yaml.safe_dump(pitch), encoding="utf-8")
    runtime_root = tmp_path / "jsb0"
    schema_dir = runtime_root / "contract" / "scenario"
    schema_dir.mkdir(parents=True)
    schema = {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": [
            "schema_version",
            "scenario_type",
            "events",
            "acceptance",
        ],
        "properties": {
            "schema_version": {"const": 1},
            "scenario_type": {"const": "pitch_hold"},
            "name": {"type": "string"},
            "aircraft": {"const": "c172x"},
            "events": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "time_sec": {"type": "number"},
                        "command": {
                            "type": "object",
                            "required": ["type", "pitch_deg"],
                            "properties": {
                                "type": {"const": "pitch_hold"},
                                "pitch_deg": {
                                    "type": "number",
                                    "minimum": -45,
                                    "maximum": 45,
                                },
                            },
                        },
                    },
                },
            },
            "acceptance": {
                "type": "object",
                "required": ["steady_state_rate_limit_deg_s"],
                "properties": {
                    "steady_state_rate_limit_deg_s": {
                        "type": "number",
                        "minimum": 0,
                    }
                },
            },
        },
    }
    (schema_dir / "scenario.schema.json").write_text(
        json.dumps(schema), encoding="utf-8"
    )

    assert validate_directory(
        scenario_dir, runtime_root, io.StringIO()
    ).passed == 1

    pitch["events"][0]["command"]["pitch_deg"] = 46
    path.write_text(yaml.safe_dump(pitch), encoding="utf-8")
    output = io.StringIO()
    assert validate_directory(scenario_dir, runtime_root, output).failed == 1
    assert "greater than the maximum" in output.getvalue()
