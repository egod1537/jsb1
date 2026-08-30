from __future__ import annotations

import math
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml

from app.domain.models import ControllerParameterDefinition
from app.services.runtime_contract import RuntimeContractError, RuntimeContractReader


class ControllerParameterError(ValueError):
    pass


@dataclass(frozen=True)
class ControllerParameterCatalog:
    source: str
    transport: str
    parameters: tuple[ControllerParameterDefinition, ...]


@dataclass(frozen=True)
class ResolvedControllerParameters:
    effective: dict[str, float]
    overrides: dict[str, float]
    by_variant: dict[str, dict[str, float]] | None = None


# Temporary adapter for JSB0 main 769f3cc. These values are copied from the
# single Runtime-owned Px4RollHoldParameterMetadata table, not from frontend UI.
PX4_ROLL_HOLD_PARAMETERS = (
    ControllerParameterDefinition(id="FW_R_TC", display_name="Roll Time Constant", symbol="T_φ", unit="s", default_value=0.4, minimum=0.2, maximum=1.0, increment=0.05, description="PX4 fixed-wing roll attitude time constant.", variants=["baseline"]),
    ControllerParameterDefinition(id="FW_R_RMAX", display_name="Maximum Roll Rate", symbol="p_max", unit="deg/s", default_value=70.0, minimum=0.0, maximum=180.0, increment=0.5, description="Maximum commanded body roll rate.", variants=["baseline"]),
    ControllerParameterDefinition(id="FW_RR_P", display_name="Roll Rate P", symbol="K_P", unit="%/rad/s", default_value=0.05, minimum=0.0, maximum=10.0, increment=0.005, description="PX4 fixed-wing roll-rate proportional gain.", variants=["baseline"]),
    ControllerParameterDefinition(id="FW_RR_I", display_name="Roll Rate I", symbol="K_I", unit="%/rad", default_value=0.1, minimum=0.0, maximum=10.0, increment=0.01, description="PX4 fixed-wing roll-rate integrator gain.", variants=["baseline"]),
    ControllerParameterDefinition(id="FW_RR_D", display_name="Roll Rate D", symbol="K_D", unit="%/rad/s", default_value=0.0, minimum=0.0, maximum=10.0, increment=0.005, description="PX4 fixed-wing roll-rate derivative gain.", variants=["baseline"]),
    ControllerParameterDefinition(id="FW_RR_FF", display_name="Roll Rate Feed Forward", symbol="K_FF", unit="%/rad/s", default_value=0.5, minimum=0.0, maximum=10.0, increment=0.05, description="PX4 fixed-wing roll-rate feed-forward gain.", variants=["baseline"]),
    ControllerParameterDefinition(id="FW_RR_IMAX", display_name="Roll Integrator Limit", symbol="I_max", unit=None, default_value=0.2, minimum=0.0, maximum=1.0, increment=0.05, description="Absolute roll-rate integrator limit.", variants=["baseline"]),
)


class RuntimeControllerParameterService:
    """Resolve controller tuning metadata and immutable execution values."""

    def __init__(self, reader: RuntimeContractReader | None = None) -> None:
        self.reader = reader or RuntimeContractReader()

    def catalog(self, runtime_root: Path) -> ControllerParameterCatalog:
        payload = self.reader.load_execution_parameter_contract(runtime_root)
        if payload is None:
            return ControllerParameterCatalog(
                source="jsb1_px4_roll_hold_adapter",
                transport="output/parameters.yaml",
                parameters=self._adapter_parameters(runtime_root),
            )
        try:
            raw_parameters = payload["parameters"]
            raw_transport = payload.get("transport", {})
            transport = raw_transport.get("path", "parameters.yaml")
            parameters = tuple(
                ControllerParameterDefinition.model_validate(item)
                for item in raw_parameters
            )
        except (KeyError, TypeError, ValueError) as exc:
            raise ControllerParameterError(
                "JSB0 controller parameter contract is invalid"
            ) from exc
        if not parameters:
            raise ControllerParameterError(
                "JSB0 controller parameter contract is empty"
            )
        if transport not in {"parameters.yaml", "output/parameters.yaml"}:
            raise ControllerParameterError(
                "JSB0 controller parameter transport must use output/parameters.yaml"
            )
        return ControllerParameterCatalog(
            source="jsb0_contract",
            transport="output/parameters.yaml",
            parameters=parameters,
        )

    def resolve(
        self,
        runtime_root: Path,
        execution_variant: str,
        requested: dict[str, float],
    ) -> ResolvedControllerParameters:
        return self.resolve_for_variants(
            runtime_root, [execution_variant], requested
        )

    def resolve_for_variants(
        self,
        runtime_root: Path,
        execution_variants: list[str],
        requested: dict[str, float],
        allowed_parameter_ids: list[str] | tuple[str, ...] | None = None,
    ) -> ResolvedControllerParameters:
        catalog = self.catalog(runtime_root)
        supported = {
            item.id: item
            for item in catalog.parameters
            if not item.variants or any(
                variant in item.variants for variant in execution_variants
            )
        }
        if allowed_parameter_ids is None:
            applicable = supported
        else:
            unsupported = sorted(set(allowed_parameter_ids) - set(supported))
            if unsupported:
                raise ControllerParameterError(
                    "Unsupported controller parameter for selected JSB0 revision: "
                    + ", ".join(unsupported)
                )
            allowed = set(allowed_parameter_ids)
            applicable = {
                item.id: item
                for item in catalog.parameters
                if item.id in allowed and item.id in supported
            }
        unknown = sorted(set(requested) - set(applicable))
        if unknown:
            raise ControllerParameterError(
                "Unsupported controller parameters for headless execution: "
                + ", ".join(unknown)
            )
        effective = {item.id: item.default_value for item in applicable.values()}
        overrides: dict[str, float] = {}
        for parameter_id, raw_value in requested.items():
            value = float(raw_value)
            if not math.isfinite(value):
                raise ControllerParameterError(f"{parameter_id} must be finite")
            definition = applicable[parameter_id]
            if definition.minimum is not None and value < definition.minimum:
                raise ControllerParameterError(
                    f"{parameter_id} must be >= {definition.minimum:g}"
                )
            if definition.maximum is not None and value > definition.maximum:
                raise ControllerParameterError(
                    f"{parameter_id} must be <= {definition.maximum:g}"
                )
            effective[parameter_id] = value
            if value != definition.default_value:
                overrides[parameter_id] = value
        by_variant = {
            variant: {
                item.id: effective[item.id]
                for item in applicable.values()
                if not item.variants or variant in item.variants
            }
            for variant in execution_variants
        }
        return ResolvedControllerParameters(
            effective=effective, overrides=overrides, by_variant=by_variant
        )

    @staticmethod
    def serialize(values: dict[str, float]) -> str:
        return yaml.safe_dump(
            {"controller_parameters": values}, sort_keys=True, allow_unicode=True
        )

    @staticmethod
    def _adapter_parameters(
        runtime_root: Path,
    ) -> tuple[ControllerParameterDefinition, ...]:
        """Read defaults from the selected JSB0 checkout when practical.

        Newer JSB0 revisions centralize the exact metadata in one constexpr
        table. Older revisions keep the defaults directly in the settings
        struct; parsing those scalar initializers preserves historical defaults
        until JSB0 publishes the JSON contract.
        """
        metadata_path = runtime_root / "src/sim/gnc/hold/Px4RollHoldParameterMetadata.hpp"
        if metadata_path.is_file():
            try:
                text = metadata_path.read_text(encoding="utf-8")
                matches = re.findall(
                    r'\{Px4RollHoldParameter::\w+,\s*"([A-Z0-9_]+)",\s*'
                    r'"([^"]+)",\s*"([^"]*)",\s*'
                    r'([-+0-9.eE]+),\s*([-+0-9.eE]+),\s*'
                    r'([-+0-9.eE]+),\s*([-+0-9.eE]+)\}',
                    text,
                )
                if len(matches) == len(PX4_ROLL_HOLD_PARAMETERS):
                    base = {item.id: item for item in PX4_ROLL_HOLD_PARAMETERS}
                    return tuple(base[parameter_id].model_copy(update={
                        "display_name": display_name,
                        "unit": unit or None,
                        "minimum": float(minimum),
                        "maximum": float(maximum),
                        "default_value": float(default_value),
                        "increment": float(increment),
                    }) for parameter_id, display_name, unit, minimum, maximum, default_value, increment in matches)
            except (OSError, UnicodeError, KeyError, ValueError):
                pass

        settings_path = runtime_root / "src/sim/gnc/hold/Px4RollHoldReferenceController.hpp"
        if not settings_path.is_file():
            return PX4_ROLL_HOLD_PARAMETERS
        try:
            text = settings_path.read_text(encoding="utf-8")
        except (OSError, UnicodeError):
            return PX4_ROLL_HOLD_PARAMETERS
        field_ids = {
            "timeConstantSec": "FW_R_TC",
            "maximumRollRateRadPerSec": "FW_R_RMAX",
            "rateProportionalGain": "FW_RR_P",
            "rateIntegralGain": "FW_RR_I",
            "rateDerivativeGain": "FW_RR_D",
            "rateFeedForwardGain": "FW_RR_FF",
            "integratorLimit": "FW_RR_IMAX",
        }
        defaults: dict[str, float] = {}
        for field, parameter_id in field_ids.items():
            match = re.search(
                rf"double\s+{re.escape(field)}\s*=\s*([-+0-9.eE]+)\s*;",
                text,
            )
            if match:
                value = float(match.group(1))
                defaults[parameter_id] = (
                    math.degrees(value) if parameter_id == "FW_R_RMAX" else value
                )
        return tuple(
            item.model_copy(update={"default_value": defaults.get(item.id, item.default_value)})
            for item in PX4_ROLL_HOLD_PARAMETERS
        )
