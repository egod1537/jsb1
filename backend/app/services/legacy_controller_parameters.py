from __future__ import annotations

import math
import re
from pathlib import Path

from app.domain.models import RuntimeParameterDefinition

# Compatibility only for pre-index JSB0 revisions. Indexed revisions never
# enter this adapter and always consume execution/parameters.json.
PX4_ROLL_HOLD_PARAMETERS = (
    RuntimeParameterDefinition(id="FW_R_TC", display_name="Roll Time Constant", symbol="T_φ", unit="s", default_value=0.4, minimum=0.2, maximum=1.0, increment=0.05, description="PX4 fixed-wing roll attitude time constant.", variants=["baseline"]),
    RuntimeParameterDefinition(id="FW_R_RMAX", display_name="Maximum Roll Rate", symbol="p_max", unit="deg/s", default_value=70.0, minimum=0.0, maximum=180.0, increment=0.5, description="Maximum commanded body roll rate.", variants=["baseline"]),
    RuntimeParameterDefinition(id="FW_RR_P", display_name="Roll Rate P", symbol="K_P", unit="%/rad/s", default_value=0.05, minimum=0.0, maximum=10.0, increment=0.005, description="PX4 fixed-wing roll-rate proportional gain.", variants=["baseline"]),
    RuntimeParameterDefinition(id="FW_RR_I", display_name="Roll Rate I", symbol="K_I", unit="%/rad", default_value=0.1, minimum=0.0, maximum=10.0, increment=0.01, description="PX4 fixed-wing roll-rate integrator gain.", variants=["baseline"]),
    RuntimeParameterDefinition(id="FW_RR_D", display_name="Roll Rate D", symbol="K_D", unit="%/rad/s", default_value=0.0, minimum=0.0, maximum=10.0, increment=0.005, description="PX4 fixed-wing roll-rate derivative gain.", variants=["baseline"]),
    RuntimeParameterDefinition(id="FW_RR_FF", display_name="Roll Rate Feed Forward", symbol="K_FF", unit="%/rad/s", default_value=0.5, minimum=0.0, maximum=10.0, increment=0.05, description="PX4 fixed-wing roll-rate feed-forward gain.", variants=["baseline"]),
    RuntimeParameterDefinition(id="FW_RR_IMAX", display_name="Roll Integrator Limit", symbol="I_max", unit=None, default_value=0.2, minimum=0.0, maximum=1.0, increment=0.05, description="Absolute roll-rate integrator limit.", variants=["baseline"]),
)


class LegacyControllerParameterAdapter:
    """Read PX4 metadata only for pre-index JSB0 revisions."""

    def parameters(self, runtime_root: Path) -> tuple[RuntimeParameterDefinition, ...]:
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
                rf"double\s+{re.escape(field)}\s*=\s*([-+0-9.eE]+)\s*;", text
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
