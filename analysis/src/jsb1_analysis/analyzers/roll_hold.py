from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np

from jsb1_analysis.io.run import RunData
from jsb1_analysis.metrics.frequency import dominant_frequency
from jsb1_analysis.metrics.time_response import calculate_step_metrics
from jsb1_analysis.telemetry import TelemetryDataset


@dataclass(frozen=True)
class RollHoldConfig:
    command_signal: str = "commanded_roll"
    roll_signal: str = "roll"
    roll_rate_signal: str = "roll_rate"
    aileron_signal: str = "aileron"
    settling_band: float = 0.02
    min_frequency_hz: float | None = 0.01
    max_frequency_hz: float | None = None


@dataclass(frozen=True)
class RollHoldResult:
    """Repeatable roll-hold metrics in canonical SI/radian telemetry units."""

    rise_time: float | None
    settling_time: float | None
    overshoot: float | None
    steady_state_error: float | None
    dominant_frequency_hz: float | None
    peak_roll_rate: float | None
    peak_aileron: float | None

    def as_dict(self) -> dict[str, float | None]:
        return asdict(self)


class RollHoldAnalyzer:
    """Analyze an explicit run or time window using canonical JSB1 signals.

    Dominant frequency is calculated from roll-rate telemetry. Peak roll rate and
    aileron are maximum absolute magnitudes. Radian/radian-per-second units are
    preserved; overshoot is a percentage and time values are seconds.
    """

    scenario_type = "roll_hold"
    required_signals = frozenset(
        {"commanded_roll", "roll", "roll_rate", "aileron"}
    )

    def __init__(self, config: RollHoldConfig | None = None) -> None:
        self.config = config or RollHoldConfig()

    def analyze(
        self,
        run: RunData | TelemetryDataset,
        *,
        variant: str | None = None,
        start_time: float | None = None,
        end_time: float | None = None,
    ) -> RollHoldResult:
        if isinstance(run, TelemetryDataset):
            return self.analyze_dataset(
                run,
                variant=variant,
                start_time=start_time,
                end_time=end_time,
            )
        segment = (
            run.slice(start_time, end_time)
            if start_time is not None or end_time is not None
            else run
        )
        command = segment.signal(self.config.command_signal)
        roll = segment.signal(self.config.roll_signal)
        roll_rate = segment.signal(self.config.roll_rate_signal)
        aileron = segment.signal(self.config.aileron_signal)
        step = calculate_step_metrics(
            segment.time,
            command,
            roll,
            settling_band=self.config.settling_band,
        )
        frequency = dominant_frequency(
            segment.time,
            roll_rate,
            min_frequency=self.config.min_frequency_hz,
            max_frequency=self.config.max_frequency_hz,
        )
        return RollHoldResult(
            rise_time=step.rise_time,
            settling_time=step.settling_time,
            overshoot=step.overshoot,
            steady_state_error=step.steady_state_error,
            dominant_frequency_hz=frequency,
            peak_roll_rate=float(np.max(np.abs(roll_rate))),
            peak_aileron=float(np.max(np.abs(aileron))),
        )

    def analyze_dataset(
        self,
        dataset: TelemetryDataset,
        *,
        variant: str | None = None,
        start_time: float | None = None,
        end_time: float | None = None,
        **_context: object,
    ) -> RollHoldResult:
        requested = [
            self.config.command_signal,
            self.config.roll_signal,
            self.config.roll_rate_signal,
            self.config.aileron_signal,
        ]
        timeline, signals = dataset.align(
            requested,
            variant=variant,
            start=start_time,
            end=end_time,
        )
        return self.analyze(RunData(timeline, signals))
