from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from numpy.typing import NDArray

from app.domain.telemetry import RuntimeSignalDefinition


@dataclass(frozen=True)
class SignalDefinition:
    """JSB1 presentation adapter for one JSB0 contract signal.

    JSB0 remains authoritative for wire fields, units, frames, and sign
    conventions. This adapter centralizes the short API id, display grouping,
    mathematical notation, and wire-to-plot conversion used by JSB1.
    """

    id: str
    display_name: str
    symbol: str
    symbol_latex: str
    unit: str
    category: str
    subcategory: str
    source_unit: str
    scale: float = 1.0

    def convert(self, values: NDArray[np.float64]) -> NDArray[np.float64]:
        return values if self.scale == 1.0 else values * self.scale


RAD_TO_DEG = 180.0 / np.pi

# JSB0 Runtime contract 2.0 catalog/telemetry RollControlState coverage.
# Additions belong here only after the Runtime contract publishes them.
LEGACY_SIGNAL_CATALOG: dict[str, SignalDefinition] = {
    "commanded_roll": SignalDefinition(
        id="commanded_roll",
        display_name="Commanded Roll",
        symbol="φc",
        symbol_latex=r"\phi_c",
        unit="deg",
        category="Command",
        subcategory="Roll",
        source_unit="rad",
        scale=RAD_TO_DEG,
    ),
    "commanded_roll_rate": SignalDefinition(
        id="commanded_roll_rate",
        display_name="Commanded Roll Rate",
        symbol="pc",
        symbol_latex=r"p_c",
        unit="deg/s",
        category="Command",
        subcategory="Roll",
        source_unit="rad/s",
        scale=RAD_TO_DEG,
    ),
    "roll": SignalDefinition(
        id="roll",
        display_name="Roll",
        symbol="φ",
        symbol_latex=r"\phi",
        unit="deg",
        category="Aircraft State",
        subcategory="Attitude",
        source_unit="rad",
        scale=RAD_TO_DEG,
    ),
    "roll_rate": SignalDefinition(
        id="roll_rate",
        display_name="Roll Rate",
        symbol="p",
        symbol_latex="p",
        unit="deg/s",
        category="Aircraft State",
        subcategory="Angular Rates",
        source_unit="rad/s",
        scale=RAD_TO_DEG,
    ),
    "roll_error": SignalDefinition(
        id="roll_error",
        display_name="Roll Error",
        symbol="eφ",
        symbol_latex=r"e_\phi",
        unit="deg",
        category="Control",
        subcategory="Tracking Error",
        source_unit="rad",
        scale=RAD_TO_DEG,
    ),
    "roll_rate_error": SignalDefinition(
        id="roll_rate_error",
        display_name="Roll Rate Error",
        symbol="ep",
        symbol_latex=r"e_p",
        unit="deg/s",
        category="Control",
        subcategory="Tracking Error",
        source_unit="rad/s",
        scale=RAD_TO_DEG,
    ),
    "aileron": SignalDefinition(
        id="aileron",
        display_name="Aileron",
        symbol="δa",
        symbol_latex=r"\delta_a",
        unit="normalized",
        category="Control",
        subcategory="Surfaces",
        source_unit="normalized",
    ),
}


def signal_definition(signal_id: str) -> SignalDefinition | None:
    return LEGACY_SIGNAL_CATALOG.get(signal_id)


# Import compatibility for callers that explicitly exercise the pre-index
# presentation adapter.
SIGNAL_CATALOG = LEGACY_SIGNAL_CATALOG


def contract_signal_definition(
    item: RuntimeSignalDefinition, api_id: str | None = None
) -> SignalDefinition:
    """Derive JSB1 presentation from contract semantics without signal tables."""
    scale = RAD_TO_DEG if item.unit in {"rad", "rad/s"} else 1.0
    unit = {"rad": "deg", "rad/s": "deg/s"}.get(item.unit, item.unit)
    group = (item.group or item.id.rsplit(".", 1)[0]).split(".")
    category = group[0].replace("_", " ").title()
    subcategory = " ".join(group[1:]).replace("_", " ").title() or "General"
    resolved_id = api_id or item.api_id
    display_name = resolved_id.rsplit(".", 1)[-1].replace("_", " ").title()
    return SignalDefinition(
        id=resolved_id,
        display_name=display_name,
        symbol="",
        symbol_latex="",
        unit=unit,
        category=category,
        subcategory=subcategory,
        source_unit=item.unit,
        scale=scale,
    )
