from __future__ import annotations

import numpy as np
from numpy.typing import ArrayLike

from app.domain.models import Metric


RAD_TO_DEG = 180.0 / np.pi
SETTLING_BAND_DEG = 0.5
STEADY_STATE_FRACTION = 0.2


def _arrays(*values: ArrayLike) -> list[np.ndarray]:
    arrays = [np.asarray(value, dtype=np.float64) for value in values]
    if any(item.ndim != 1 for item in arrays) or len({len(item) for item in arrays}) != 1:
        raise ValueError("metric inputs must be equally sized one-dimensional arrays")
    if not arrays or len(arrays[0]) < 2:
        raise ValueError("at least two samples are required")
    return arrays


def compute_roll_hold_metrics(
    time_sec: ArrayLike,
    commanded_roll_rad: ArrayLike,
    roll_rad: ArrayLike,
    aileron_rad: ArrayLike,
) -> list[Metric]:
    time, command, roll, aileron = _arrays(
        time_sec, commanded_roll_rad, roll_rad, aileron_rad
    )
    if np.any(np.diff(time) < 0):
        raise ValueError("time must be monotonic")
    command_deg = command * RAD_TO_DEG
    roll_deg = roll * RAD_TO_DEG
    aileron_deg = aileron * RAD_TO_DEG
    onset_candidates = np.flatnonzero(np.abs(command_deg - command_deg[0]) > 1e-6)
    onset = int(onset_candidates[0]) if onset_candidates.size else 0
    error = command_deg - roll_deg
    post_error = error[onset:]

    settled = np.abs(post_error) <= SETTLING_BAND_DEG
    suffix_settled = np.logical_and.accumulate(settled[::-1])[::-1]
    settling_candidates = np.flatnonzero(suffix_settled)
    settling_time: float | None = (
        float(time[onset + settling_candidates[0]] - time[onset])
        if settling_candidates.size
        else None
    )

    target_delta = command_deg[-1] - command_deg[0]
    direction = 1.0 if target_delta >= 0 else -1.0
    overshoot = max(
        0.0,
        float(np.max(direction * (roll_deg[onset:] - command_deg[onset:]))),
    )
    rms = float(np.sqrt(np.mean(np.square(post_error))))
    steady_count = max(1, int(np.ceil(len(error) * STEADY_STATE_FRACTION)))
    steady_error = float(abs(np.mean(error[-steady_count:])))
    max_aileron = float(np.max(np.abs(aileron_deg)))

    return [
        Metric(name="settling_time_sec", value=settling_time, unit="s"),
        Metric(name="overshoot_deg", value=overshoot, unit="deg"),
        Metric(name="rms_error_deg", value=rms, unit="deg"),
        Metric(name="steady_state_error_deg", value=steady_error, unit="deg"),
        Metric(name="max_abs_aileron_deg", value=max_aileron, unit="deg"),
    ]
