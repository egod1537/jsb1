from __future__ import annotations

import numpy as np
import pytest

from jsb1_analysis.analyzers import (
    AnalyzerMissingSignalError,
    AnalyzerRegistry,
    CourseHoldAnalyzer,
    shortest_angular_distance,
)
from jsb1_analysis.telemetry import SignalSeries, TelemetryDataset


def course_dataset(*, include_roll: bool = True) -> TelemetryDataset:
    time = np.arange(0.0, 8.0, 1.0)
    degrees = {
        "course.commanded": [10.0] * 8,
        "course.actual": [350.0, 355.0, 1.0, 7.0, 11.0, 10.5, 10.1, 10.0],
        "course.error": [20.0, 15.0, 9.0, 3.0, -1.0, -0.5, -0.1, 0.0],
        "roll_setpoint": [0.0, 12.0, 15.0, 9.0, -2.0, -1.0, 0.0, 0.0],
        "roll": [0.0, 5.0, 12.0, 10.0, 2.0, -1.0, 0.0, 0.0],
    }
    series = {
        ("baseline", name): SignalSeries(time, np.deg2rad(values), "rad")
        for name, values in degrees.items()
        if include_roll or name != "roll"
    }
    series[("baseline", "roll_limited")] = SignalSeries(
        time, [0, 1, 1, 0, 0, 0, 0, 0], "boolean"
    )
    series[("baseline", "roll_setpoint_rate_limited")] = SignalSeries(
        time, [0, 1, 0, 0, 0, 0, 0, 0], "boolean"
    )
    return TelemetryDataset(series, contract_variants=("baseline",))


def test_course_hold_metrics_are_wrap_aware() -> None:
    result = CourseHoldAnalyzer().analyze_dataset(
        course_dataset(), variant="baseline"
    )
    assert result.rms_course_error_rad < np.deg2rad(10.0)
    assert result.steady_state_course_error_rad == pytest.approx(
        0.0, abs=np.deg2rad(0.2)
    )
    assert result.overshoot_rad == pytest.approx(np.deg2rad(1.0))
    assert result.max_roll_setpoint_rad == pytest.approx(np.deg2rad(15.0))
    assert result.roll_limit_duration_s == 2.0
    assert result.roll_limit_ratio == pytest.approx(2.0 / 7.0)


def test_shortest_distance_handles_359_to_1_degrees() -> None:
    distance = shortest_angular_distance(
        np.deg2rad(np.array([359.0])), np.deg2rad(np.array([1.0]))
    )
    assert distance[0] == pytest.approx(np.deg2rad(2.0))


def test_course_hold_uses_scenario_settling_band() -> None:
    result = CourseHoldAnalyzer().analyze_dataset(
        course_dataset(),
        variant="baseline",
        scenario_definition={"acceptance": {"settling_band_deg": 0.25}},
    )
    assert result.settling_time_s == 6.0


def test_course_hold_registry_reports_missing_signals() -> None:
    registry = AnalyzerRegistry((CourseHoldAnalyzer(),))
    with pytest.raises(AnalyzerMissingSignalError, match="roll"):
        registry.analyze("course_hold", course_dataset(include_roll=False), variant="baseline")
