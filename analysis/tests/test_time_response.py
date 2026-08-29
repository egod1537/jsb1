from __future__ import annotations

import numpy as np
import pytest

from jsb1_analysis.metrics.time_response import calculate_step_metrics


def first_order_response() -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    time = np.linspace(0.0, 10.0, 1001)
    command = np.where(time >= 1.0, 1.0, 0.0)
    response = np.where(time >= 1.0, 1.0 - np.exp(-(time - 1.0)), 0.0)
    return time, command, response


def test_clean_first_order_step_metrics() -> None:
    time, command, response = first_order_response()
    result = calculate_step_metrics(time, command, response)
    assert result.rise_time == pytest.approx(2.20, abs=0.03)
    assert result.settling_time == pytest.approx(3.92, abs=0.03)
    assert result.overshoot == pytest.approx(0.0)
    assert result.steady_state_error is not None
    assert result.steady_state_error < 0.001
    assert result.peak_value is not None
    assert result.peak_time is not None


def test_overshoot_is_percent_of_command_step() -> None:
    time = np.linspace(0.0, 10.0, 2001)
    command = np.where(time >= 1.0, 1.0, 0.0)
    elapsed = np.maximum(time - 1.0, 0.0)
    response = np.where(
        time >= 1.0,
        1.0 - np.exp(-0.6 * elapsed) * np.cos(3.0 * elapsed),
        0.0,
    )
    result = calculate_step_metrics(time, command, response)
    assert result.overshoot is not None
    assert result.overshoot > 30.0


def test_non_settling_response_and_constant_command() -> None:
    time = np.linspace(0.0, 10.0, 1001)
    command = np.where(time >= 1.0, 1.0, 0.0)
    response = np.where(time >= 1.0, 1.0 + 0.1 * np.cos(2 * np.pi * time), 0.0)
    result = calculate_step_metrics(time, command, response)
    assert result.settling_time is None

    constant = calculate_step_metrics(time, np.ones_like(time), np.full_like(time, 0.9))
    assert constant.rise_time is None
    assert constant.overshoot is None
    assert constant.settling_time is None
    assert constant.steady_state_error == pytest.approx(0.1)


def test_invalid_metric_inputs_are_rejected() -> None:
    with pytest.raises(ValueError, match="equal lengths"):
        calculate_step_metrics([0, 1, 2], [0, 1], [0, 1, 1])
    with pytest.raises(ValueError, match="finite"):
        calculate_step_metrics([0, 1, 2], [0, 1, 1], [0, np.nan, 1])
