from __future__ import annotations

import numpy as np
from numpy.typing import NDArray


def uniform_downsample(
    time: NDArray[np.float64],
    series: dict[str, NDArray[np.float64]],
    max_points: int,
) -> tuple[NDArray[np.float64], dict[str, NDArray[np.float64]]]:
    """Keep endpoints and select uniformly spaced source indices."""
    if len(time) <= max_points:
        return time, series
    indices = np.linspace(0, len(time) - 1, num=max_points, dtype=np.int64)
    indices = np.unique(indices)
    return time[indices], {name: values[indices] for name, values in series.items()}

