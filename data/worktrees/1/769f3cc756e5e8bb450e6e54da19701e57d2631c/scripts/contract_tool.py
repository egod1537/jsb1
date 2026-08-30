#!/usr/bin/env python3
"""Dependency-free generation, validation, and export for the JSB0 contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


PROTO_FILES = (
    "telemetry/common.proto",
    "telemetry/aircraft_state.proto",
    "telemetry/control.proto",
    "telemetry/simulation.proto",
)
REQUIRED_SIGNALS = {
    "simulation.time",
    "aircraft.roll",
    "aircraft.roll_rate",
    "control.commanded_roll",
    "control.commanded_roll_rate",
    "actuator.aileron",
}


class ContractError(RuntimeError):
    pass


def parse_scalar(text: str) -> Any:
    value = text.strip()
    if value in {"null", "~"}:
        return None
    if value == "true":
        return True
    if value == "false":
        return False
    if value.startswith("["):
        try:
            return json.loads(value)
        except json.JSONDecodeError as error:
            raise ContractError(f"invalid inline YAML sequence: {value}") from error
    if (value.startswith('"') and value.endswith('"')) or (
        value.startswith("'") and value.endswith("'")
    ):
        return value[1:-1]
    try:
        number = float(value)
        if not math.isfinite(number):
            raise ValueError
        return int(number) if number.is_integer() and "." not in value else number
    except ValueError:
        return value


def load_yaml_mapping(path: Path) -> dict[str, Any]:
    lines: list[tuple[int, int, str]] = []
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw_line.strip() or raw_line.lstrip().startswith("#"):
            continue
        if "\t" in raw_line:
            raise ContractError(f"{path}:{line_number}: tabs are not valid indentation")
        indent = len(raw_line) - len(raw_line.lstrip(" "))
        if indent % 2 != 0:
            raise ContractError(f"{path}:{line_number}: indentation must use two spaces")
        lines.append((line_number, indent, raw_line.strip()))

    def parse_block(index: int, indent: int) -> tuple[Any, int]:
        is_sequence = lines[index][2].startswith("- ")
        container: Any = [] if is_sequence else {}
        while index < len(lines):
            line_number, line_indent, content = lines[index]
            if line_indent < indent:
                break
            if line_indent != indent:
                raise ContractError(f"{path}:{line_number}: unexpected indentation")
            if is_sequence:
                if not content.startswith("- "):
                    break
                entry = content[2:].strip()
                if ":" not in entry:
                    raise ContractError(f"{path}:{line_number}: expected a mapping sequence item")
                key, value = entry.split(":", 1)
                item: dict[str, Any] = {}
                item[key.strip()] = parse_scalar(value) if value.strip() else None
                index += 1
                while index < len(lines) and lines[index][1] > indent:
                    child_line, child_indent, child_content = lines[index]
                    if child_indent != indent + 2 or ":" not in child_content:
                        raise ContractError(f"{path}:{child_line}: invalid sequence item indentation")
                    child_key, child_value = child_content.split(":", 1)
                    child_key = child_key.strip()
                    if child_key in item:
                        raise ContractError(f"{path}:{child_line}: duplicate key {child_key}")
                    if child_value.strip():
                        item[child_key] = parse_scalar(child_value)
                        index += 1
                    else:
                        index += 1
                        if index >= len(lines) or lines[index][1] <= child_indent:
                            item[child_key] = {}
                        else:
                            item[child_key], index = parse_block(index, child_indent + 2)
                container.append(item)
            else:
                if content.startswith("- ") or ":" not in content:
                    raise ContractError(f"{path}:{line_number}: expected a mapping entry")
                key, value = content.split(":", 1)
                key = key.strip()
                if not key or key in container:
                    raise ContractError(f"{path}:{line_number}: invalid or duplicate key {key}")
                index += 1
                if value.strip():
                    container[key] = parse_scalar(value)
                elif index < len(lines) and lines[index][1] > indent:
                    container[key], index = parse_block(index, indent + 2)
                else:
                    container[key] = {}
        return container, index

    if not lines:
        raise ContractError(f"{path}: document is empty")
    result, consumed = parse_block(0, lines[0][1])
    if consumed != len(lines) or not isinstance(result, dict):
        raise ContractError(f"{path}: root must be a mapping")
    return result


def type_matches(instance: Any, expected: str) -> bool:
    if expected == "object":
        return isinstance(instance, dict)
    if expected == "array":
        return isinstance(instance, list)
    if expected == "string":
        return isinstance(instance, str)
    if expected == "boolean":
        return isinstance(instance, bool)
    if expected == "integer":
        return isinstance(instance, int) and not isinstance(instance, bool)
    if expected == "number":
        return isinstance(instance, (int, float)) and not isinstance(instance, bool)
    return True


def validate_instance(instance: Any, schema: dict[str, Any], location: str = "$") -> list[str]:
    errors: list[str] = []
    if "const" in schema and instance != schema["const"]:
        errors.append(f"{location}: expected constant {schema['const']!r}")
    if "enum" in schema and instance not in schema["enum"]:
        errors.append(f"{location}: value {instance!r} is not in {schema['enum']!r}")
    expected_type = schema.get("type")
    if expected_type and not type_matches(instance, expected_type):
        return errors + [f"{location}: expected {expected_type}"]
    if isinstance(instance, dict):
        properties = schema.get("properties", {})
        for required in schema.get("required", []):
            if required not in instance:
                errors.append(f"{location}: missing required property {required}")
        if schema.get("additionalProperties") is False:
            for key in instance:
                if key not in properties:
                    errors.append(f"{location}: unexpected property {key}")
        for key, value in instance.items():
            if key in properties:
                errors.extend(validate_instance(value, properties[key], f"{location}.{key}"))
    if isinstance(instance, list):
        if len(instance) < schema.get("minItems", 0):
            errors.append(f"{location}: array is shorter than minItems")
        item_schema = schema.get("items")
        if item_schema:
            for index, value in enumerate(instance):
                errors.extend(validate_instance(value, item_schema, f"{location}[{index}]"))
    if isinstance(instance, str):
        if len(instance) < schema.get("minLength", 0):
            errors.append(f"{location}: string is shorter than minLength")
        if schema.get("format") == "date-time" and not re.fullmatch(
            r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z", instance
        ):
            errors.append(f"{location}: expected an RFC3339 UTC date-time")
        if "pattern" in schema and not re.fullmatch(schema["pattern"], instance):
            errors.append(f"{location}: string does not match required pattern")
    if isinstance(instance, (int, float)) and not isinstance(instance, bool):
        if not math.isfinite(float(instance)):
            errors.append(f"{location}: number must be finite")
        if "minimum" in schema and instance < schema["minimum"]:
            errors.append(f"{location}: value is below minimum")
        if "exclusiveMinimum" in schema and instance <= schema["exclusiveMinimum"]:
            errors.append(f"{location}: value is not above exclusiveMinimum")
    return errors


def validate_execution_metadata(metadata: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    mode = metadata.get("mode")
    execution = metadata.get("execution", {})
    if mode == "single":
        if (
            set(execution) != {"variant"}
            or "autopilot" not in metadata
            or "results" in metadata
        ):
            errors.append(
                "single metadata must contain one variant/autopilot and no comparison results"
            )
    elif mode == "compare":
        if (
            execution != {"variants": ["baseline", "primary"]}
            or "autopilot" in metadata
            or "results" not in metadata
        ):
            errors.append(
                "compare metadata must contain canonical variants/results and no autopilot"
            )
    return errors


def parse_proto_messages(contract_root: Path) -> dict[str, dict[str, str]]:
    messages: dict[str, dict[str, str]] = {}
    for relative in PROTO_FILES:
        source = (contract_root / relative).read_text(encoding="utf-8")
        package_match = re.search(r"\bpackage\s+([\w.]+)\s*;", source)
        if not package_match:
            raise ContractError(f"{relative}: missing package")
        package = package_match.group(1)
        for message_match in re.finditer(r"\bmessage\s+(\w+)\s*\{([^}]*)\}", source, re.DOTALL):
            fields = {
                field.group(2): field.group(1)
                for field in re.finditer(
                    r"(?:optional\s+|repeated\s+)?([\w.]+)\s+(\w+)\s*=\s*\d+\s*;",
                    message_match.group(2),
                )
            }
            messages[f"{package}.{message_match.group(1)}"] = fields
    return messages


def run_protoc(contract_root: Path, protoc: Path, output: Path, python: bool) -> None:
    output.mkdir(parents=True, exist_ok=True)
    command = [str(protoc), f"--proto_path={contract_root}"]
    if python:
        command.append(f"--python_out={output}")
    else:
        command.extend(
            ["--include_imports", f"--descriptor_set_out={output / 'jsb_telemetry_v1.pb'}"]
        )
    command.extend(PROTO_FILES)
    completed = subprocess.run(command, cwd=contract_root, text=True, capture_output=True)
    if completed.returncode != 0:
        raise ContractError(completed.stderr.strip() or "protoc failed")


def validate_contract(root: Path, protoc: Path, output: Path) -> None:
    contract_root = root / "contract"
    version = (contract_root / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[1-9]\d*\.\d+\.\d+", version):
        raise ContractError("VERSION must be a semantic version with a non-zero major")

    scenario_schema = json.loads((contract_root / "scenario/scenario.schema.json").read_text(encoding="utf-8"))
    metadata_schema = json.loads((contract_root / "metadata/run.schema.json").read_text(encoding="utf-8"))
    variants_schema = json.loads((contract_root / "execution/variants.schema.json").read_text(encoding="utf-8"))
    capabilities_schema = json.loads((contract_root / "execution/capabilities.schema.json").read_text(encoding="utf-8"))
    for name, schema in (("scenario", scenario_schema), ("metadata", metadata_schema), ("execution variants", variants_schema), ("execution capabilities", capabilities_schema)):
        if schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
            raise ContractError(f"{name} schema must declare JSON Schema draft 2020-12")

    scenario = load_yaml_mapping(contract_root / "examples/scenario/roll_hold.yaml")
    metadata = json.loads((contract_root / "examples/metadata/run.json").read_text(encoding="utf-8"))
    variants = json.loads((contract_root / "execution/variants.json").read_text(encoding="utf-8"))
    capabilities = json.loads((contract_root / "execution/capabilities.json").read_text(encoding="utf-8"))
    errors = validate_instance(scenario, scenario_schema)
    errors.extend(validate_instance(metadata, metadata_schema))
    errors.extend(validate_instance(variants, variants_schema))
    errors.extend(validate_instance(capabilities, capabilities_schema))
    if variants.get("variants") != ["baseline", "primary"]:
        errors.append("$.variants must expose baseline and primary in canonical order")
    if capabilities != {
        "modes": ["single", "compare"],
        "variants": ["baseline", "primary"],
        "compare_variants": ["baseline", "primary"],
    }:
        errors.append("execution capabilities do not expose canonical modes and variants")
    errors.extend(validate_execution_metadata(metadata))
    scenario_digest = hashlib.sha256(
        (contract_root / "examples/scenario/roll_hold.yaml").read_bytes()
    ).hexdigest()
    if metadata.get("scenario", {}).get("digest_sha256") != scenario_digest:
        errors.append("example metadata scenario digest does not match scenario bytes")
    duration = scenario.get("simulation", {}).get("duration_sec", 0)
    dt = scenario.get("simulation", {}).get("dt_sec", 0)
    if dt and not math.isclose(duration / dt, round(duration / dt), abs_tol=1e-9):
        errors.append("$.simulation.duration_sec must be an integer multiple of $.simulation.dt_sec")
    previous_time = -1
    for index, event in enumerate(scenario.get("events", [])):
        event_time = event.get("time_sec", -1)
        if event_time < previous_time:
            errors.append(f"$.events[{index}].time_sec must be ordered")
        if event_time >= duration:
            errors.append(f"$.events[{index}].time_sec must be less than $.simulation.duration_sec")
        if dt and not math.isclose(event_time / dt, round(event_time / dt), abs_tol=1e-9):
            errors.append(f"$.events[{index}].time_sec must align to $.simulation.dt_sec")
        previous_time = event_time
    invalid_scenario = load_yaml_mapping(contract_root / "tests/invalid_scenario.yaml")
    if not validate_instance(invalid_scenario, scenario_schema):
        errors.append("invalid scenario fixture was unexpectedly accepted")

    catalog = load_yaml_mapping(contract_root / "catalog/signals.yaml")
    if catalog.get("contract_version") != version:
        errors.append("catalog contract_version does not match VERSION")
    messages = parse_proto_messages(contract_root)
    topics = catalog.get("topics", {})
    signals = catalog.get("signals", {})
    missing_signals = REQUIRED_SIGNALS - set(signals)
    if missing_signals:
        errors.append(f"catalog is missing required signals: {sorted(missing_signals)}")
    for topic, definition in topics.items():
        message = definition.get("message") if isinstance(definition, dict) else None
        if message not in messages:
            errors.append(f"topic {topic} references unknown message {message}")
    for logical_name, definition in signals.items():
        if not isinstance(definition, dict):
            errors.append(f"signal {logical_name} must be a mapping")
            continue
        for key in ("topic", "field", "type", "unit", "frame", "convention", "required"):
            if key not in definition:
                errors.append(f"signal {logical_name} is missing {key}")
        topic = definition.get("topic")
        topic_definition = topics.get(topic, {}) if isinstance(topics, dict) else {}
        message = topic_definition.get("message") if isinstance(topic_definition, dict) else None
        if message not in messages or definition.get("field") not in messages.get(message, set()):
            errors.append(
                f"signal {logical_name} references unknown field {message}.{definition.get('field')}"
            )
            continue
        field_name = definition.get("field")
        proto_type = messages[message][field_name]
        expected_catalog_type = {"double": "float64", "uint64": "uint64"}.get(proto_type)
        if expected_catalog_type and definition.get("type") != expected_catalog_type:
            errors.append(
                f"signal {logical_name} type {definition.get('type')} does not match {proto_type}"
            )
        suffix_units = {"_rad_s": "rad/s", "_rad": "rad", "_ns": "ns"}
        for suffix, unit in suffix_units.items():
            if field_name.endswith(suffix) and definition.get("unit") != unit:
                errors.append(
                    f"signal {logical_name} unit {definition.get('unit')} does not match {field_name}"
                )
                break

    if errors:
        raise ContractError("contract validation failed:\n  - " + "\n  - ".join(errors))
    run_protoc(contract_root, protoc, output, python=False)
    print(f"contract {version}: validation passed")


def export_contract(root: Path, output: Path) -> None:
    source = root / "contract"
    if output.exists():
        shutil.rmtree(output)
    shutil.copytree(source, output, ignore=shutil.ignore_patterns("tests", "__pycache__"))
    print(f"exported contract to {output}")


def validate_run_metadata(root: Path, run_json: Path) -> None:
    schema = json.loads(
        (root / "contract/metadata/run.schema.json").read_text(encoding="utf-8")
    )
    metadata = json.loads(run_json.read_text(encoding="utf-8"))
    errors = validate_instance(metadata, schema)
    errors.extend(validate_execution_metadata(metadata))
    expected_version = (root / "contract/VERSION").read_text(encoding="utf-8").strip()
    if metadata.get("contract_version") != expected_version:
        errors.append("$.contract_version does not match contract/VERSION")
    if errors:
        raise ContractError("run metadata validation failed:\n  - " + "\n  - ".join(errors))
    print(f"validated run metadata {run_json}")


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    for command in ("validate", "generate-python"):
        subparser = subparsers.add_parser(command)
        subparser.add_argument("--root", type=Path, required=True)
        subparser.add_argument("--protoc", type=Path, required=True)
        subparser.add_argument("--output", type=Path, required=True)
    export_parser = subparsers.add_parser("export")
    export_parser.add_argument("--root", type=Path, required=True)
    export_parser.add_argument("--output", type=Path, required=True)
    run_parser = subparsers.add_parser("validate-run")
    run_parser.add_argument("--root", type=Path, required=True)
    run_parser.add_argument("--run-json", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        if arguments.command == "validate":
            validate_contract(arguments.root.resolve(), arguments.protoc.resolve(), arguments.output.resolve())
        elif arguments.command == "generate-python":
            run_protoc(
                arguments.root.resolve() / "contract",
                arguments.protoc.resolve(),
                arguments.output.resolve(),
                python=True,
            )
            print(f"generated Python contract types in {arguments.output.resolve()}")
        elif arguments.command == "export":
            export_contract(arguments.root.resolve(), arguments.output.resolve())
        else:
            validate_run_metadata(
                arguments.root.resolve(), arguments.run_json.resolve()
            )
    except (ContractError, OSError, json.JSONDecodeError) as error:
        print(f"contract error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
