from __future__ import annotations

import numpy as np
from jsb1_analysis.metrics.primitives import (
    absolute_overshoot,
    peak_absolute,
    rms_error,
    settling_time,
    steady_state_error,
)
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
    onset_candidates = np.flatnonzero(np.abs(command - command[0]) > 1e-9)
    onset = int(onset_candidates[0]) if onset_candidates.size else 0
    error = command - roll
    settling = settling_time(
        time,
        error,
        band=SETTLING_BAND_DEG / RAD_TO_DEG,
        onset=onset,
    )
    overshoot = absolute_overshoot(command, roll, onset=onset) * RAD_TO_DEG
    rms = rms_error(command[onset:], roll[onset:]) * RAD_TO_DEG
    steady_error = steady_state_error(
        command,
        roll,
        fraction=STEADY_STATE_FRACTION,
    ) * RAD_TO_DEG
    max_aileron = peak_absolute(aileron) * RAD_TO_DEG

    return [
        Metric(name="settling_time_sec", value=settling, unit="s"),
        Metric(name="overshoot_deg", value=overshoot, unit="deg"),
        Metric(name="rms_error_deg", value=rms, unit="deg"),
        Metric(name="steady_state_error_deg", value=steady_error, unit="deg"),
        Metric(name="max_abs_aileron_deg", value=max_aileron, unit="deg"),
    ]
