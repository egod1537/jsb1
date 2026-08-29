from __future__ import annotations

import json

import numpy as np
import pytest

from jsb1_analysis.analyzers.roll_hold import RollHoldAnalyzer, RollHoldConfig
from jsb1_analysis.io.run import RunData, SignalNotFoundError


def synthetic_roll_hold() -> RunData:
    time = np.linspace(0.0, 10.0, 2000, endpoint=False)
    command = np.where(time >= 1.0, np.deg2rad(5.0), 0.0)
    roll = np.where(time >= 1.0, command * (1.0 - np.exp(-(time - 1.0))), 0.0)
    roll_rate = 0.1 * np.sin(2.0 * np.pi * 1.5 * time)
    aileron = np.clip((command - roll) * 0.4, -0.2, 0.2)
    return RunData(
        time=time,
        signals={
            "commanded_roll": command,
            "roll": roll,
            "roll_rate": roll_rate,
            "aileron": aileron,
        },
    )


def test_roll_hold_analyzer_returns_repeatable_metrics() -> None:
    result = RollHoldAnalyzer().analyze(synthetic_roll_hold())
    assert result.rise_time == pytest.approx(2.2, abs=0.04)
    assert result.settling_time == pytest.approx(3.92, abs=0.04)
    assert result.overshoot == pytest.approx(0.0)
    assert result.dominant_frequency_hz == pytest.approx(1.5, abs=0.02)
    assert result.peak_roll_rate == pytest.approx(0.1, rel=1e-3)
    assert result.peak_aileron is not None
    assert (
        json.loads(json.dumps(result.as_dict(), allow_nan=False))["rise_time"]
        is not None
    )


def test_explicit_window_and_signal_mapping() -> None:
    run = synthetic_roll_hold()
    config = RollHoldConfig(command_signal="roll_cmd")
    result = RollHoldAnalyzer(config).analyze(run, start_time=0.5, end_time=8.0)
    assert result.rise_time is not None


def test_missing_signal_names_the_signal() -> None:
    run = synthetic_roll_hold()
    incomplete = RunData(
        time=run.time,
        signals={
            name: values for name, values in run.signals.items() if name != "aileron"
        },
    )
    with pytest.raises(SignalNotFoundError, match="aileron"):
        RollHoldAnalyzer().analyze(incomplete)
