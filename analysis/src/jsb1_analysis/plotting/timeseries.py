from __future__ import annotations

from typing import Any

import matplotlib.pyplot as plt

from jsb1_analysis.analyzers.roll_hold import RollHoldConfig
from jsb1_analysis.io.run import RunData


def plot_roll_hold(
    run: RunData,
    *,
    config: RollHoldConfig | None = None,
    start: float | None = None,
    end: float | None = None,
) -> tuple[Any, Any]:
    """Plot command/roll, roll rate, and aileron without calculating metrics."""

    selected = run.slice(start, end) if start is not None or end is not None else run
    signals = config or RollHoldConfig()
    figure, axes = plt.subplots(
        3, 1, sharex=True, figsize=(11, 7), constrained_layout=True
    )
    axes[0].plot(
        selected.time, selected.signal(signals.command_signal), label="commanded roll"
    )
    axes[0].plot(selected.time, selected.signal(signals.roll_signal), label="roll")
    axes[0].set_ylabel("angle [rad]")
    axes[0].legend(loc="best")
    axes[1].plot(
        selected.time, selected.signal(signals.roll_rate_signal), label="roll rate p"
    )
    axes[1].set_ylabel("rate [rad/s]")
    axes[1].legend(loc="best")
    axes[2].plot(
        selected.time, selected.signal(signals.aileron_signal), label="aileron"
    )
    axes[2].set_ylabel("deflection [rad]")
    axes[2].set_xlabel("time [s]")
    axes[2].legend(loc="best")
    for axis in axes:
        axis.grid(True, alpha=0.3)
    return figure, axes
