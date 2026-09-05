from __future__ import annotations

import numpy as np
import pytest

from jsb1_analysis.analyzers import (
    AnalyzerMissingSignalError,
    AnalyzerRegistry,
    TecsAnalyzer,
)
from jsb1_analysis.telemetry import SignalSeries, TelemetryDataset


def tecs_dataset(*, include_throttle: bool = True) -> TelemetryDataset:
    time = np.linspace(0.0, 30.0, 301)
    altitude_target = np.where(time < 5.0, 100.0, 150.0)
    airspeed_target = np.where(time < 5.0, 40.0, 45.0)
    altitude = altitude_target - np.where(time < 5.0, 0.0, np.exp(-(time - 5.0) / 2.0))
    airspeed = airspeed_target - np.where(time < 5.0, 0.0, 0.5 * np.exp(-(time - 5.0) / 2.0))
    data = {
        "tecs.altitude.target": altitude_target,
        "tecs.altitude.actual": altitude,
        "tecs.airspeed.target": airspeed_target,
        "tecs.airspeed.actual": airspeed,
        "tecs.pitch_target": 0.05 * np.sin(time) * np.exp(-time / 10.0),
        "tecs.underspeed_active": (time < 1.0).astype(float),
        "tecs.throttle_upper_saturated": ((time >= 5.0) & (time < 6.0)).astype(float),
        "saturation_positive": ((time >= 6.0) & (time < 6.5)).astype(float),
    }
    if include_throttle:
        data["tecs.throttle_target"] = 0.5 + 0.02 * np.sin(time)
    return TelemetryDataset({
        ("baseline", name): SignalSeries(time, value, "")
        for name, value in data.items()
    })


def test_tecs_tail_protection_saturation_and_safe_airspeed_metrics() -> None:
    result = TecsAnalyzer().analyze_dataset(
        tecs_dataset(), variant="baseline", minimum_safe_airspeed_mps=39.0,
        maximum_safe_airspeed_mps=50.0,
        scenario_definition={"acceptance": {
            "altitude_error_limit_m": 2.0,
            "airspeed_error_limit_mps": 0.3,
        }},
    )
    assert result.tail_window_s == pytest.approx(10.0)
    assert result.steady_state_rms_altitude_error_m < 0.01
    assert result.steady_state_rms_airspeed_error_mps < 0.01
    assert result.throttle_saturation_duration_s == pytest.approx(1.0)
    assert result.elevator_saturation_duration_s == pytest.approx(0.5)
    assert result.elevator_saturation_ratio == pytest.approx(0.5 / 25.0)
    assert result.underspeed_protection_activated is True
    assert result.overspeed_protection_activated is None
    assert result.safe_airspeed_violation_duration_s == pytest.approx(0.0)


def test_tecs_registry_reports_missing_required_signal() -> None:
    with pytest.raises(AnalyzerMissingSignalError) as error:
        AnalyzerRegistry((TecsAnalyzer(),)).analyze(
            "tecs", tecs_dataset(include_throttle=False), variant="baseline"
        )
    assert "tecs.throttle_target" in error.value.missing
