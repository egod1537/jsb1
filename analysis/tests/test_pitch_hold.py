from __future__ import annotations

import numpy as np
import pytest

from jsb1_analysis.analyzers import (
    AnalyzerMissingSignalError,
    AnalyzerRegistry,
    PitchHoldAnalyzer,
    PitchHoldConfig,
)
from jsb1_analysis.telemetry import SignalSeries, TelemetryDataset


def pitch_dataset(
    *, include_elevator: bool = True, include_limits: bool = True
) -> TelemetryDataset:
    time = np.arange(0.0, 9.0, 1.0)
    values = {
        "pitch.commanded": [0, 0, 5, 5, 5, 5, 5, 5, 5],
        "pitch.actual": [0, 0, 2, 4, 5.5, 5.1, 5.02, 5, 5],
        "pitch_rate.actual": [0, 0, 3, 2, 1, 0.2, 0.05, 0.02, 0],
        "elevator": [0, 0, 0.6, 0.4, -0.1, 0, 0, 0, 0],
    }
    series = {
        ("baseline", name): SignalSeries(
            time,
            data if name == "elevator" else np.deg2rad(data),
            "normalized" if name == "elevator" else (
                "rad/s" if name == "pitch_rate.actual" else "rad"
            ),
        )
        for name, data in values.items()
        if include_elevator or name != "elevator"
    }
    if include_limits:
        series[("baseline", "saturation_positive")] = SignalSeries(
            time, [0, 0, 1, 0, 0, 0, 0, 0, 0], "boolean"
        )
        series[("baseline", "saturation_negative")] = SignalSeries(
            time, np.zeros(time.size), "boolean"
        )
        series[("baseline", "integrator_limited")] = SignalSeries(
            time, [0, 0, 0, 1, 0, 0, 0, 0, 0], "boolean"
        )
    return TelemetryDataset(series, contract_variants=("baseline",))


def test_pitch_hold_tail_response_and_saturation_metrics() -> None:
    result = PitchHoldAnalyzer().analyze_dataset(
        pitch_dataset(),
        variant="baseline",
        scenario_definition={"acceptance": {"settling_band_deg": 0.2}},
    )
    assert np.rad2deg(result.steady_state_mean_pitch_error_rad) == pytest.approx(
        -0.02 / 3.0
    )
    assert np.rad2deg(result.steady_state_max_abs_pitch_error_rad) == pytest.approx(
        0.02
    )
    assert np.rad2deg(result.steady_state_max_abs_pitch_rate_rad_s) == pytest.approx(
        0.05
    )
    assert np.rad2deg(result.overshoot_rad) == pytest.approx(0.5)
    assert result.settling_time_s == 3.0
    assert np.rad2deg(result.pitch_peak_to_peak_rad) == pytest.approx(3.5)
    assert result.max_abs_elevator == 0.6
    assert result.elevator_saturation_duration_s == 1.0
    assert result.elevator_saturation_ratio == pytest.approx(1.0 / 6.0)
    assert result.integrator_limit_duration_s == 1.0
    assert result.tail_window_s == 2.0


def test_pitch_hold_tail_window_is_configurable() -> None:
    result = PitchHoldAnalyzer(
        PitchHoldConfig(tail_window_sec=1.0)
    ).analyze_dataset(pitch_dataset(), variant="baseline")
    assert result.tail_window_s == 1.0
    assert np.rad2deg(result.steady_state_rms_pitch_rate_rad_s) == pytest.approx(
        np.sqrt((0.02**2 + 0.0**2) / 2.0)
    )


def test_pitch_hold_optional_limit_signals_are_descriptive() -> None:
    result = PitchHoldAnalyzer().analyze_dataset(
        pitch_dataset(include_limits=False), variant="baseline"
    )
    assert result.elevator_saturation_duration_s is None
    assert result.elevator_saturation_ratio is None
    assert result.integrator_limit_duration_s is None


def test_pitch_hold_registry_reports_missing_required_signal() -> None:
    registry = AnalyzerRegistry((PitchHoldAnalyzer(),))
    with pytest.raises(AnalyzerMissingSignalError, match="elevator"):
        registry.analyze(
            "pitch_hold",
            pitch_dataset(include_elevator=False),
            variant="baseline",
        )

