from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path

import yaml
from jsonschema.exceptions import ValidationError

from app.domain.models import RuntimeParameterDefinition
from app.services.legacy_controller_parameters import LegacyControllerParameterAdapter
from app.services.runtime_contract import (
    RuntimeContractError,
    RuntimeContractReader,
    RuntimeParameterMismatch,
)


class ControllerParameterError(RuntimeParameterMismatch):
    pass


@dataclass(frozen=True)
class ControllerParameterCatalog:
    source: str
    transport: str
    parameters: tuple[RuntimeParameterDefinition, ...]


@dataclass(frozen=True)
class ResolvedControllerParameters:
    effective: dict[str, float]
    overrides: dict[str, float]
    by_variant: dict[str, dict[str, float]] | None = None


class RuntimeControllerParameterService:
    """Resolve typed parameter metadata from one exact Runtime contract."""

    def __init__(
        self,
        reader: RuntimeContractReader | None = None,
        legacy: LegacyControllerParameterAdapter | None = None,
    ) -> None:
        self.reader = reader or RuntimeContractReader()
        self.legacy = legacy or LegacyControllerParameterAdapter()

    def catalog(self, runtime_root: Path) -> ControllerParameterCatalog:
        if not self.reader.is_indexed(runtime_root):
            payload = self.reader.load_execution_parameter_contract(runtime_root)
            if payload is None:
                return ControllerParameterCatalog(
                    source="jsb1_px4_roll_hold_adapter",
                    transport="output/parameters.yaml",
                    parameters=self.legacy.parameters(runtime_root),
                )
        try:
            parameters = self.reader.load_parameters(runtime_root)
            artifact_path = self.reader.load_artifact_manifest(runtime_root).path_for(
                "parameter_set_snapshot"
            )
        except RuntimeContractError:
            raise
        except (OSError, RuntimeError, ValueError) as exc:
            raise ControllerParameterError(
                "JSB0 controller parameter contract is invalid"
            ) from exc
        return ControllerParameterCatalog(
            source="jsb0_contract",
            transport=f"output/{artifact_path}",
            parameters=parameters,
        )

    def resolve(
        self,
        runtime_root: Path,
        execution_variant: str,
        requested: dict[str, float],
    ) -> ResolvedControllerParameters:
        return self.resolve_for_variants(runtime_root, [execution_variant], requested)

    def resolve_for_variants(
        self,
        runtime_root: Path,
        execution_variants: list[str],
        requested: dict[str, float],
        allowed_parameter_ids: list[str] | tuple[str, ...] | None = None,
    ) -> ResolvedControllerParameters:
        catalog = self.catalog(runtime_root)
        all_definitions = {item.id: item for item in catalog.parameters}
        if allowed_parameter_ids is None:
            allowed = set(all_definitions)
        else:
            missing = sorted(set(allowed_parameter_ids) - set(all_definitions))
            if missing:
                raise ControllerParameterError(
                    "Unsupported controller parameter for selected JSB0 revision: "
                    + ", ".join(missing)
                )
            allowed = set(allowed_parameter_ids)

        supported = {
            item.id: item
            for item in catalog.parameters
            if item.id in allowed
            and (not item.variants or any(
                variant in item.variants for variant in execution_variants
            ))
        }
        unknown = sorted(set(requested) - set(supported))
        if unknown:
            raise ControllerParameterError(
                "Unsupported controller parameters for headless execution: "
                + ", ".join(unknown)
            )
        normalized = self._validate_values(runtime_root, requested, catalog.source)
        effective = {item.id: item.default_value for item in supported.values()}
        effective.update(normalized)
        overrides = {
            parameter_id: value
            for parameter_id, value in normalized.items()
            if value != supported[parameter_id].default_value
        }
        by_variant = {
            variant: {
                item.id: effective[item.id]
                for item in supported.values()
                if not item.variants or variant in item.variants
            }
            for variant in execution_variants
        }
        return ResolvedControllerParameters(effective, overrides, by_variant)

    def _validate_values(
        self, runtime_root: Path, requested: dict[str, float], source: str
    ) -> dict[str, float]:
        normalized: dict[str, float] = {}
        for parameter_id, raw_value in requested.items():
            if isinstance(raw_value, bool):
                raise ControllerParameterError(f"{parameter_id} must be numeric")
            value = float(raw_value)
            if not math.isfinite(value):
                raise ControllerParameterError(f"{parameter_id} must be finite")
            normalized[parameter_id] = value

        if source == "jsb0_contract" and self.reader.is_indexed(runtime_root):
            validator = self.reader.load_parameter_set_schema(runtime_root)
            try:
                validator.validate({"controller_parameters": normalized})
            except ValidationError as exc:
                path = ".".join(str(item) for item in exc.absolute_path)
                prefix = f"{path}: " if path else ""
                raise ControllerParameterError(
                    f"Invalid JSB0 parameter set: {prefix}{exc.message}"
                ) from exc
            return normalized

        definitions = {item.id: item for item in self.catalog(runtime_root).parameters}
        for parameter_id, value in normalized.items():
            definition = definitions[parameter_id]
            if definition.minimum is not None and value < definition.minimum:
                raise ControllerParameterError(
                    f"{parameter_id} must be >= {definition.minimum:g}"
                )
            if definition.maximum is not None and value > definition.maximum:
                raise ControllerParameterError(
                    f"{parameter_id} must be <= {definition.maximum:g}"
                )
        return normalized

    @staticmethod
    def serialize(values: dict[str, float]) -> str:
        payload = {"controller_parameters": values}
        # JSON's finite-number restriction is the same boundary expected by the
        # Runtime JSON Schema even though the transport itself is YAML.
        json.dumps(payload, allow_nan=False)
        return yaml.safe_dump(payload, sort_keys=True, allow_unicode=True)
