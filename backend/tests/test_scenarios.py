from __future__ import annotations

import json
from pathlib import Path

import pytest

from app.domain.scenario_source import ScenarioSourceType
from app.repositories.database import Database
from app.repositories.scenario_catalog import ScenarioCatalogRepository
from app.services.scenario_inspection import ScenarioInspectionService
from app.services.scenarios import InvalidScenario, ScenarioService
from app.domain.scenario_validation import ScenarioRuntime, ScenarioValidationPolicy
from app.services.scenario_sync import CompatibilityContract
from app.services.scenario_validator import ScenarioValidator
from app.services.scenario_writes import (
    ManagedScenarioConflict,
    ManagedScenarioPathError,
    ManagedScenarioValidationFailed,
    ScenarioWriteService,
)


def test_scenario_autopilot_is_loaded_and_validated_by_runtime_contract(
    tmp_path: Path,
) -> None:
    scenario_dir = tmp_path / "scenarios"
    scenario_dir.mkdir()
    (scenario_dir / "roll.yaml").write_text(
        "name: Roll hold\nautopilot:\n  type: baseline\n", encoding="utf-8"
    )
    runtime = tmp_path / "runtime"
    schema_dir = runtime / "contract" / "scenario"
    schema_dir.mkdir(parents=True)
    (schema_dir / "scenario.schema.json").write_text(
        json.dumps(
            {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "required": ["name", "autopilot"],
                "properties": {
                    "name": {"type": "string"},
                    "autopilot": {
                        "type": "object",
                        "required": ["type"],
                        "properties": {
                            "type": {"enum": ["baseline", "primary"]},
                        },
                    },
                },
                "additionalProperties": False,
            }
        ),
        encoding="utf-8",
    )

    service = ScenarioService(scenario_dir)
    definition = service.load("roll.yaml")

    assert definition.legacy_autopilot == "baseline"
    assert definition.source_type is ScenarioSourceType.BUNDLED
    service.validate_runtime_contract(definition, runtime)


def test_runtime_contract_rejects_unsupported_autopilot(tmp_path: Path) -> None:
    scenario_dir = tmp_path / "scenarios"
    scenario_dir.mkdir()
    (scenario_dir / "roll.yaml").write_text(
        "name: Roll hold\nautopilot:\n  type: experimental\n", encoding="utf-8"
    )
    runtime = tmp_path / "runtime"
    schema_dir = runtime / "contract" / "scenario"
    schema_dir.mkdir(parents=True)
    (schema_dir / "scenario.schema.json").write_text(
        json.dumps(
            {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "properties": {
                    "autopilot": {
                        "type": "object",
                        "properties": {
                            "type": {"enum": ["baseline", "primary"]},
                        },
                    },
                },
            }
        ),
        encoding="utf-8",
    )

    definition = ScenarioService(scenario_dir).load("roll.yaml")
    with pytest.raises(
        InvalidScenario,
        match="Scenario requires unsupported autopilot 'experimental'",
    ):
        ScenarioService(scenario_dir).validate_runtime_contract(definition, runtime)


def test_scenario_type_can_be_recovered_from_historical_snapshot(tmp_path: Path) -> None:
    scenario_dir = tmp_path / "scenarios"
    scenario_dir.mkdir()
    snapshot = tmp_path / "run-scenario.yaml"
    snapshot.write_text(
        "schema_version: 1\nscenario_type: roll_hold\nname: Historical run\nautopilot: baseline\n",
        encoding="utf-8",
    )
    service = ScenarioService(scenario_dir)
    assert service.scenario_type_from_snapshot(snapshot) == "roll_hold"
    assert service.scenario_type_from_snapshot(tmp_path / "missing.yaml") is None


def test_managed_scenario_is_validated_published_and_loadable(tmp_path: Path) -> None:
    runtime = tmp_path / "runtime"
    schema_dir = runtime / "contract" / "scenario"
    schema_dir.mkdir(parents=True)
    (schema_dir / "scenario.schema.json").write_text(
        json.dumps(
            {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "additionalProperties": False,
                "required": ["schema_version", "scenario_type", "name"],
                "properties": {
                    "schema_version": {"const": 1},
                    "scenario_type": {"const": "roll_hold"},
                    "name": {"type": "string", "minLength": 1},
                },
            }
        ),
        encoding="utf-8",
    )

    class Resolver:
        def resolve(self):
            return CompatibilityContract(
                runtime,
                ScenarioRuntime(branch="main", commit="a" * 40),
            )

    managed = tmp_path / "data" / "scenarios" / "managed"
    writer = ScenarioWriteService(managed, ScenarioValidator(), Resolver())
    yaml_text = "schema_version: 1\nscenario_type: roll_hold\nname: Managed\n"

    created = writer.create("team/managed.yaml", yaml_text)

    assert created.source == "managed"
    assert created.validation.valid is True
    assert created.validation.runtime is not None
    assert created.validation.runtime.commit == "a" * 40
    assert (managed / "team" / "managed.yaml").read_text(encoding="utf-8") == yaml_text
    service = ScenarioService(
        tmp_path / "bundled",
        managed_scenario_dir=managed,
    )
    managed_definition = service.load("team/managed.yaml", "managed")
    assert managed_definition.name == "Managed"
    assert managed_definition.source_type is ScenarioSourceType.MANAGED
    assert service.catalog()[0].source == "managed"

    with pytest.raises(ManagedScenarioConflict):
        writer.create("team/managed.yaml", yaml_text)
    with pytest.raises(ManagedScenarioPathError):
        writer.create("../escape.yaml", yaml_text)
    with pytest.raises(ManagedScenarioPathError):
        writer.create("/absolute.yaml", yaml_text)


def test_invalid_managed_scenario_is_never_written(tmp_path: Path) -> None:
    runtime = tmp_path / "runtime"
    schema_dir = runtime / "contract" / "scenario"
    schema_dir.mkdir(parents=True)
    (schema_dir / "scenario.schema.json").write_text(
        json.dumps(
            {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "required": ["name"],
                "properties": {"name": {"type": "string"}},
            }
        ),
        encoding="utf-8",
    )

    class Resolver:
        def resolve(self):
            return CompatibilityContract(
                runtime,
                ScenarioRuntime(branch="main", commit="b" * 40),
            )

    managed = tmp_path / "managed"
    writer = ScenarioWriteService(managed, ScenarioValidator(), Resolver())
    with pytest.raises(ManagedScenarioValidationFailed) as captured:
        writer.create("invalid.yaml", "name: [unterminated\n")
    assert captured.value.validation.valid is False
    assert not (managed / "invalid.yaml").exists()


def test_catalog_filters_with_stable_contract_and_run_uses_exact_policy(
    tmp_path: Path,
) -> None:
    scenario_dir = tmp_path / "scenarios"
    scenario_dir.mkdir()
    (scenario_dir / "valid.yaml").write_text("name: Valid\n", encoding="utf-8")
    (scenario_dir / "future.yaml").write_text(
        "name: Future\nfuture: true\n", encoding="utf-8"
    )
    runtime = tmp_path / "runtime"
    schema_dir = runtime / "contract" / "scenario"
    schema_dir.mkdir(parents=True)
    (schema_dir / "scenario.schema.json").write_text(
        json.dumps(
            {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "required": ["name"],
                "properties": {"name": {"type": "string"}},
                "additionalProperties": False,
            }
        ),
        encoding="utf-8",
    )

    class Resolver:
        def resolve(self):
            return CompatibilityContract(
                runtime,
                ScenarioRuntime(branch="main", commit="c" * 40),
            )

    class TrackingValidator(ScenarioValidator):
        policy = None

        def evaluate_document(self, document, contract, **kwargs):
            self.policy = kwargs["policy"]
            return super().evaluate_document(document, contract, **kwargs)

    validator = TrackingValidator()
    service = ScenarioService(
        scenario_dir,
        validator,
        stable_contract_resolver=Resolver(),
    )

    assert [item.id for item in service.catalog()] == ["valid.yaml"]
    invalid = service.invalid_catalog()
    assert invalid[0].id == "future.yaml"
    assert invalid[0].errors[0]["code"] == "additionalProperties"

    service.validate_runtime_contract(service.load("valid.yaml"), runtime)
    assert validator.policy is ScenarioValidationPolicy.RUN_EXACT


def test_inspection_preserves_raw_yaml_from_the_shared_failed_parse(
    tmp_path: Path,
) -> None:
    scenarios_root = tmp_path / "scenarios"
    scenarios_root.mkdir()
    raw = "name: [unterminated\n"
    (scenarios_root / "broken.yaml").write_text(raw, encoding="utf-8")
    runtime = tmp_path / "runtime"
    schema_root = runtime / "contract" / "scenario"
    schema_root.mkdir(parents=True)
    (schema_root / "scenario.schema.json").write_text(
        json.dumps({"type": "object"}), encoding="utf-8"
    )
    database = Database(
        tmp_path / "data" / "jsb1.db",
        Path(__file__).resolve().parents[1] / "migrations",
    )
    database.initialize()
    validator = ScenarioValidator()
    service = ScenarioService(scenarios_root, validator)
    inspector = ScenarioInspectionService(
        service,
        validator,
        ScenarioCatalogRepository(database),
    )

    detail = inspector.detail(
        "bundled",
        "broken.yaml",
        validator.load_runtime_contract(runtime),
        ScenarioRuntime(branch="main", commit="d" * 40),
    )

    assert detail.raw_yaml == raw
    assert detail.definition is None
    assert detail.validation.errors[0]["code"] == "yaml_parse"
