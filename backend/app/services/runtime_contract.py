from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml
from jsonschema import Draft202012Validator
from jsonschema.exceptions import SchemaError

from app.domain.scenario_validation import ScenarioValidationResult
from app.services.scenario_validator import ScenarioValidationUnavailable, ScenarioValidator


class RuntimeContractError(ValueError):
    pass


@dataclass(frozen=True)
class HeadlessExecutionCapabilities:
    mode: str
    variants: tuple[str, ...]
    authoritative: bool = True


class RuntimeContractReader:
    """The single owner of the JSB0 Runtime contract filesystem layout."""

    SCENARIO_SCHEMA = Path("contract/scenario/scenario.schema.json")
    EXECUTION_VARIANTS = Path("contract/execution/variants.json")
    EXECUTION_CAPABILITIES = Path("contract/execution/capabilities.json")
    EXECUTION_CAPABILITIES_ALTERNATE = Path("execution/capabilities.json")
    EXECUTION_PARAMETERS = Path("contract/execution/parameters.json")
    TELEMETRY_CATALOG = Path("contract/catalog/signals.yaml")
    CONTRACT_VERSION = Path("contract/version.json")

    def load_scenario_schema(self, runtime_root: Path) -> Draft202012Validator:
        path = runtime_root / self.SCENARIO_SCHEMA
        if not path.is_file():
            raise ScenarioValidationUnavailable(
                "JSB0 Runtime scenario schema is unavailable"
            )
        try:
            schema = json.loads(path.read_text(encoding="utf-8"))
            Draft202012Validator.check_schema(schema)
        except (OSError, UnicodeError, json.JSONDecodeError, SchemaError) as exc:
            raise ScenarioValidationUnavailable(
                f"invalid JSB0 Runtime scenario schema: {exc}"
            ) from exc
        return Draft202012Validator(schema)

    def load_headless_capabilities(
        self, runtime_root: Path
    ) -> HeadlessExecutionCapabilities:
        path = runtime_root / self.EXECUTION_CAPABILITIES
        if not path.is_file():
            alternate = runtime_root / self.EXECUTION_CAPABILITIES_ALTERNATE
            if alternate.is_file():
                path = alternate
        if path.is_file():
            try:
                payload = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, UnicodeError, json.JSONDecodeError) as exc:
                raise RuntimeContractError(
                    "JSB0 headless execution capability contract is invalid"
                ) from exc
            return self._headless_capability_values(payload)

        legacy_path = runtime_root / self.EXECUTION_VARIANTS
        if legacy_path.is_file():
            try:
                payload = json.loads(legacy_path.read_text(encoding="utf-8"))
            except (OSError, UnicodeError, json.JSONDecodeError) as exc:
                raise RuntimeContractError(
                    "JSB0 execution variant contract is invalid"
                ) from exc
            variants = self._variant_values(payload)
        else:
            variants = self._legacy_variant_values(runtime_root)
        normalized = sorted(set(variants))
        if not normalized:
            raise RuntimeContractError(
                "JSB0 Runtime does not declare any execution variants"
            )
        return HeadlessExecutionCapabilities(
            mode="single", variants=tuple(normalized), authoritative=False
        )

    def load_execution_capabilities(self, runtime_root: Path) -> list[str]:
        return list(self.load_headless_capabilities(runtime_root).variants)

    def load_execution_parameter_contract(
        self, runtime_root: Path
    ) -> dict[str, Any] | None:
        path = runtime_root / self.EXECUTION_PARAMETERS
        if not path.is_file():
            return None
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            raise RuntimeContractError(
                "JSB0 controller parameter contract is invalid"
            ) from exc
        if not isinstance(payload, dict):
            raise RuntimeContractError(
                "JSB0 controller parameter contract must be an object"
            )
        return payload

    def load_telemetry_catalog(self, runtime_root: Path) -> dict[str, Any] | None:
        path = runtime_root / self.TELEMETRY_CATALOG
        if not path.is_file():
            return None
        try:
            payload = yaml.safe_load(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, yaml.YAMLError) as exc:
            raise RuntimeContractError("JSB0 telemetry contract is invalid") from exc
        if not isinstance(payload, dict):
            raise RuntimeContractError("JSB0 telemetry contract must be an object")
        return payload

    def load_contract_version(self, runtime_root: Path) -> str | None:
        path = runtime_root / self.CONTRACT_VERSION
        if not path.is_file():
            return None
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            raise RuntimeContractError("JSB0 contract version is invalid") from exc
        value = payload.get("version") if isinstance(payload, dict) else payload
        return str(value) if isinstance(value, (str, int)) else None

    @staticmethod
    def _variant_values(payload: Any) -> list[str]:
        raw = payload if isinstance(payload, list) else payload.get("variants") if isinstance(payload, dict) else None
        if not isinstance(raw, list):
            raise RuntimeContractError(
                "execution variant contract must contain a variants array"
            )
        values: list[str] = []
        for item in raw:
            value = item if isinstance(item, str) else item.get("id", item.get("name")) if isinstance(item, dict) else None
            if not isinstance(value, str) or not value.strip():
                raise RuntimeContractError("execution variant entries require an id")
            values.append(value.strip())
        return values

    @classmethod
    def _headless_capability_values(
        cls, payload: Any
    ) -> HeadlessExecutionCapabilities:
        if not isinstance(payload, dict):
            raise RuntimeContractError(
                "headless execution capabilities must be an object"
            )
        headless = payload.get("headless", payload)
        if not isinstance(headless, dict):
            raise RuntimeContractError("headless capabilities must be an object")
        mode = headless.get("mode", headless.get("headless_mode"))
        if mode is None:
            modes = headless.get("modes")
            if isinstance(modes, list) and len(modes) == 1:
                mode = modes[0]
        execution = headless.get("execution")
        if mode is None and isinstance(execution, dict):
            mode = execution.get("mode")
        variants_payload: Any = headless
        if mode == "compare" and "compare_variants" in headless:
            variants_payload = {"variants": headless["compare_variants"]}
        elif "variants" not in headless and isinstance(execution, dict):
            variants_payload = execution
        variants = tuple(dict.fromkeys(cls._variant_values(variants_payload)))
        if not isinstance(mode, str) or not mode.strip():
            raise RuntimeContractError("headless capabilities require a mode")
        if not variants:
            raise RuntimeContractError(
                "headless capabilities require execution variants"
            )
        normalized_mode = mode.strip().lower().replace("_", "-")
        if normalized_mode in {"compare-only", "comparison"}:
            normalized_mode = "compare"
        return HeadlessExecutionCapabilities(
            mode=normalized_mode, variants=variants, authoritative=True
        )

    def _legacy_variant_values(self, runtime_root: Path) -> list[str]:
        validator = self.load_scenario_schema(runtime_root)
        schema = validator.schema
        autopilot = schema.get("properties", {}).get("autopilot", {})
        if isinstance(autopilot.get("enum"), list):
            return [item for item in autopilot["enum"] if isinstance(item, str)]
        nested = autopilot.get("properties", {}).get("type", {})
        if isinstance(nested.get("enum"), list):
            return [item for item in nested["enum"] if isinstance(item, str)]
        raise RuntimeContractError(
            "JSB0 execution variant contract is unavailable"
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
            self.validator.load_runtime_contract(runtime_root),
            runtime_branch=runtime_branch,
            runtime_commit=runtime_commit,
        )

    def load_execution_capabilities(self, runtime_root: Path) -> list[str]:
        return self.reader.load_execution_capabilities(runtime_root)
