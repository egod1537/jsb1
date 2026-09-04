from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from numpy.typing import ArrayLike, NDArray


@dataclass(frozen=True)
class SaturationMetric:
    duration: float
    ratio: float


def rms_error(reference: ArrayLike, response: ArrayLike) -> float:
    expected, actual = aligned_arrays(reference, response)
    return float(np.sqrt(np.mean(np.square(expected - actual))))


def steady_state_error(
    reference: ArrayLike,
    response: ArrayLike,
    *,
    fraction: float = 0.2,
) -> float:
    if not 0 < fraction <= 1:
        raise ValueError("fraction must be in (0, 1]")
    expected, actual = aligned_arrays(reference, response)
    count = max(1, int(np.ceil(expected.size * fraction)))
    return float(abs(np.mean(expected[-count:] - actual[-count:])))


def absolute_overshoot(
    reference: ArrayLike,
    response: ArrayLike,
    *,
    onset: int = 0,
    direction: float | None = None,
) -> float:
    expected, actual = aligned_arrays(reference, response)
    if not 0 <= onset < expected.size:
        raise ValueError("onset is outside the signal")
    if direction is None:
        selected_direction = 1.0 if expected[-1] - expected[0] >= 0 else -1.0
    else:
        selected_direction = 1.0 if direction >= 0 else -1.0
    return max(
        0.0,
        float(np.max(selected_direction * (actual[onset:] - expected[onset:]))),
    )


def settling_time(
    time: ArrayLike,
    error: ArrayLike,
    *,
    band: float,
    onset: int = 0,
) -> float | None:
    timeline, values = aligned_arrays(time, error)
    if band < 0:
        raise ValueError("band must be non-negative")
    if not 0 <= onset < timeline.size:
        raise ValueError("onset is outside the signal")
    within = np.abs(values[onset:]) <= band
    stays_within = np.logical_and.accumulate(within[::-1])[::-1]
    candidates = np.flatnonzero(stays_within)
    if not candidates.size:
        return None
    return float(timeline[onset + candidates[0]] - timeline[onset])


def peak_to_peak(values: ArrayLike) -> float:
    (array,) = aligned_arrays(values)
    return float(np.ptp(array))


def peak_absolute(values: ArrayLike) -> float:
    (array,) = aligned_arrays(values)
    return float(np.max(np.abs(array)))


def saturation_metric(
    time: ArrayLike,
    values: ArrayLike,
    *,
    limit: float,
) -> SaturationMetric:
    timeline, signal = aligned_arrays(time, values)
    if limit < 0:
        raise ValueError("limit must be non-negative")
    active = np.abs(signal) >= limit
    if timeline.size < 2:
        return SaturationMetric(0.0, 0.0)
    duration = float(np.sum(np.diff(timeline)[active[:-1]]))
    total = float(timeline[-1] - timeline[0])
    return SaturationMetric(duration, duration / total if total > 0 else 0.0)


def aligned_arrays(*values: ArrayLike) -> tuple[NDArray[np.float64], ...]:
    arrays = tuple(np.asarray(value, dtype=np.float64) for value in values)
    if not arrays or any(item.ndim != 1 for item in arrays):
        raise ValueError("metric inputs must be one-dimensional")
    if len({item.size for item in arrays}) != 1:
        raise ValueError("metric inputs must have equal lengths")
    if arrays[0].size == 0:
        raise ValueError("metric inputs must not be empty")
    if any(not np.all(np.isfinite(item)) for item in arrays):
        raise ValueError("metric inputs must contain only finite values")
    return arrays
