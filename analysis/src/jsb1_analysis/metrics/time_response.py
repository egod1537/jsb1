from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from numpy.typing import ArrayLike, NDArray


@dataclass(frozen=True)
class StepResponseMetrics:
    """Metrics for a single commanded step.

    ``rise_time`` is the elapsed 10%-to-90% response crossing time.
    ``settling_time`` is elapsed from detected command onset until response stays
    within ``settling_band * abs(command step)`` of the final command.
    ``overshoot`` is percent of command step amplitude. ``steady_state_error`` is
    the absolute final-command error using the mean of the last 10% of samples.
    ``peak_time`` is expressed on the supplied time axis.
    """

    rise_time: float | None
    settling_time: float | None
    overshoot: float | None
    steady_state_error: float | None
    peak_value: float | None
    peak_time: float | None


def _validated_inputs(
    time: ArrayLike, command: ArrayLike, response: ArrayLike
) -> tuple[NDArray[np.float64], NDArray[np.float64], NDArray[np.float64]]:
    arrays = tuple(
        np.asarray(value, dtype=np.float64) for value in (time, command, response)
    )
    if any(value.ndim != 1 for value in arrays):
        raise ValueError("time, command, and response must be one-dimensional")
    if len({value.size for value in arrays}) != 1:
        raise ValueError("time, command, and response must have equal lengths")
    if arrays[0].size < 3:
        raise ValueError("at least three samples are required")
    if any(not np.all(np.isfinite(value)) for value in arrays):
        raise ValueError("time, command, and response must contain only finite values")
    if np.any(np.diff(arrays[0]) <= 0):
        raise ValueError("time must be strictly increasing")
    return arrays


def calculate_step_metrics(
    time: ArrayLike,
    command: ArrayLike,
    response: ArrayLike,
    *,
    settling_band: float = 0.02,
) -> StepResponseMetrics:
    """Calculate deterministic step-response metrics from aligned signal arrays.

    Command onset is the first sample at least 5% of the final step away from the
    initial command. The final command is the median of the last 10% of command
    samples. A constant command has no rise time or overshoot, but still produces
    steady-state, peak, and (when applicable) settling metrics.
    """

    if not 0 < settling_band < 1:
        raise ValueError("settling_band must be between 0 and 1")
    timeline, commanded, actual = _validated_inputs(time, command, response)
    tail_count = max(1, int(np.ceil(timeline.size * 0.1)))
    initial_command = float(commanded[0])
    final_command = float(np.median(commanded[-tail_count:]))
    step = final_command - initial_command
    step_epsilon = max(np.max(np.abs(commanded)) * 1e-9, 1e-12)
    has_step = abs(step) > step_epsilon

    if has_step:
        onset_candidates = np.flatnonzero(
            np.abs(commanded - initial_command) >= abs(step) * 0.05
        )
        onset = int(onset_candidates[0]) if onset_candidates.size else 0
    else:
        onset = 0
    baseline_response = (
        float(np.median(actual[:onset])) if onset > 0 else float(actual[0])
    )
    post_response = actual[onset:]
    post_time = timeline[onset:]

    rise_time: float | None = None
    overshoot: float | None = None
    direction = np.sign(final_command - baseline_response)
    if has_step and direction != 0:
        ten = baseline_response + 0.1 * (final_command - baseline_response)
        ninety = baseline_response + 0.9 * (final_command - baseline_response)
        crossed_ten = np.flatnonzero(direction * (post_response - ten) >= 0)
        crossed_ninety = np.flatnonzero(direction * (post_response - ninety) >= 0)
        if crossed_ten.size and crossed_ninety.size:
            first_ten = int(crossed_ten[0])
            later_ninety = crossed_ninety[crossed_ninety >= first_ten]
            if later_ninety.size:
                rise_time = float(
                    post_time[int(later_ninety[0])] - post_time[first_ten]
                )
        excursion = float(np.max(direction * (post_response - final_command)))
        overshoot = max(0.0, excursion) / abs(step) * 100.0

    band_scale = abs(step) if has_step else max(abs(final_command), 1.0)
    within_band = np.abs(post_response - final_command) <= settling_band * band_scale
    stays_within = np.logical_and.accumulate(within_band[::-1])[::-1]
    settling_candidates = np.flatnonzero(stays_within)
    settling_time = (
        float(post_time[int(settling_candidates[0])] - post_time[0])
        if settling_candidates.size
        else None
    )

    steady_state_error = abs(final_command - float(np.mean(actual[-tail_count:])))
    if direction > 0:
        peak_index = onset + int(np.argmax(post_response))
    elif direction < 0:
        peak_index = onset + int(np.argmin(post_response))
    else:
        peak_index = int(np.argmax(np.abs(actual)))

    return StepResponseMetrics(
        rise_time=rise_time,
        settling_time=settling_time,
        overshoot=overshoot,
        steady_state_error=steady_state_error,
        peak_value=float(actual[peak_index]),
        peak_time=float(timeline[peak_index]),
    )
