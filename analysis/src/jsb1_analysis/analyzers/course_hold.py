from __future__ import annotations

from collections.abc import Mapping
from dataclasses import asdict, dataclass

import numpy as np
from numpy.typing import NDArray

from jsb1_analysis.telemetry import MissingSignalError, TelemetryDataset


def shortest_angular_distance(
    actual: NDArray[np.float64], commanded: NDArray[np.float64]
) -> NDArray[np.float64]:
    """Signed shortest distance from actual to commanded in radians."""

    return (commanded - actual + np.pi) % (2.0 * np.pi) - np.pi


@dataclass(frozen=True)
class CourseHoldConfig:
    commanded_signal: str = "course.commanded"
    course_signal: str = "course.actual"
    error_signal: str = "course.error"
    roll_setpoint_signal: str = "roll_setpoint"
    roll_signal: str = "roll"
    roll_limited_signal: str = "roll_limited"
    roll_rate_limited_signal: str = "roll_setpoint_rate_limited"
    settling_band_rad: float = np.deg2rad(1.0)
    steady_state_fraction: float = 0.1


@dataclass(frozen=True)
class CourseHoldResult:
    steady_state_course_error_rad: float
    rms_course_error_rad: float
    overshoot_rad: float
    settling_time_s: float | None
    oscillation_count: int
    max_roll_setpoint_rad: float
    roll_limit_duration_s: float | None
    roll_limit_ratio: float | None
    roll_setpoint_rate_limit_duration_s: float | None
    roll_setpoint_rate_limit_ratio: float | None

    def as_dict(self) -> dict[str, float | int | None]:
        return asdict(self)


class CourseHoldAnalyzer:
    scenario_type = "course_hold"
    required_signals = frozenset(
        {"course.commanded", "course.actual", "course.error", "roll_setpoint", "roll"}
    )

    def __init__(self, config: CourseHoldConfig | None = None) -> None:
        self.config = config or CourseHoldConfig()

    def analyze_dataset(
        self,
        dataset: TelemetryDataset,
        *,
        variant: str | None = None,
        start_time: float | None = None,
        end_time: float | None = None,
        scenario_definition: object | None = None,
        **_context: object,
    ) -> CourseHoldResult:
        names = [
            self.config.commanded_signal,
            self.config.course_signal,
            self.config.error_signal,
            self.config.roll_setpoint_signal,
            self.config.roll_signal,
        ]
        time, values = dataset.align(
            names, variant=variant, start=start_time, end=end_time
        )
        commanded = values[self.config.commanded_signal]
        actual = values[self.config.course_signal]
        # JSB0 defines the signed Course error. Canonicalize that contract value
        # at the angular boundary without changing its sign convention.
        error = (values[self.config.error_signal] + np.pi) % (2.0 * np.pi) - np.pi
        settling_band = self._settling_band(scenario_definition)
        tail = max(1, int(np.ceil(time.size * self.config.steady_state_fraction)))
        onset = self._command_onset(commanded)
        settling = self._settling_time(time, error, onset, settling_band)
        overshoot = self._overshoot(commanded, actual, onset)
        roll_duration, roll_ratio = self._limiter_metric(
            dataset, self.config.roll_limited_signal, time, variant
        )
        rate_duration, rate_ratio = self._limiter_metric(
            dataset, self.config.roll_rate_limited_signal, time, variant
        )
        return CourseHoldResult(
            steady_state_course_error_rad=float(np.mean(error[-tail:])),
            rms_course_error_rad=float(np.sqrt(np.mean(np.square(error)))),
            overshoot_rad=overshoot,
            settling_time_s=settling,
            oscillation_count=self._oscillation_count(
                error[onset:], settling_band
            ),
            max_roll_setpoint_rad=float(
                np.max(np.abs(values[self.config.roll_setpoint_signal]))
            ),
            roll_limit_duration_s=roll_duration,
            roll_limit_ratio=roll_ratio,
            roll_setpoint_rate_limit_duration_s=rate_duration,
            roll_setpoint_rate_limit_ratio=rate_ratio,
        )

    @staticmethod
    def _command_onset(commanded: NDArray[np.float64]) -> int:
        delta = shortest_angular_distance(
            np.full_like(commanded, commanded[0]), commanded
        )
        candidates = np.flatnonzero(np.abs(delta) > 1.0e-9)
        return int(candidates[0]) if candidates.size else 0

    def _settling_time(
        self,
        time: NDArray[np.float64],
        error: NDArray[np.float64],
        onset: int,
        settling_band: float,
    ) -> float | None:
        outside = np.flatnonzero(np.abs(error[onset:]) > settling_band)
        if outside.size == 0:
            return 0.0
        candidate = onset + int(outside[-1]) + 1
        if candidate >= time.size:
            return None
        return float(time[candidate] - time[onset])

    def _settling_band(self, scenario_definition: object | None) -> float:
        if not isinstance(scenario_definition, Mapping):
            return self.config.settling_band_rad
        acceptance = scenario_definition.get("acceptance")
        if not isinstance(acceptance, Mapping):
            return self.config.settling_band_rad
        value = acceptance.get("settling_band_deg")
        if not isinstance(value, (int, float)) or not np.isfinite(value) or value < 0:
            return self.config.settling_band_rad
        return float(np.deg2rad(value))

    @staticmethod
    def _overshoot(
        commanded: NDArray[np.float64], actual: NDArray[np.float64], onset: int
    ) -> float:
        target = float(commanded[-1])
        baseline = float(actual[max(0, onset - 1)])
        step = float(shortest_angular_distance(
            np.array([baseline]), np.array([target])
        )[0])
        if abs(step) <= 1.0e-12:
            return 0.0
        response = baseline + np.unwrap(actual[onset:] - baseline)
        target_unwrapped = baseline + step
        directed_excess = np.sign(step) * (response - target_unwrapped)
        return float(max(0.0, np.max(directed_excess)))

    @staticmethod
    def _oscillation_count(error: NDArray[np.float64], deadband: float) -> int:
        signs = np.sign(np.where(np.abs(error) <= deadband, 0.0, error))
        signs = signs[signs != 0.0]
        if signs.size < 2:
            return 0
        return int(np.count_nonzero(signs[1:] != signs[:-1]) // 2)

    @staticmethod
    def _limiter_metric(
        dataset: TelemetryDataset,
        signal: str,
        timeline: NDArray[np.float64],
        variant: str | None,
    ) -> tuple[float | None, float | None]:
        try:
            series = dataset.signal(signal, variant)
        except MissingSignalError:
            return None, None
        active = np.interp(timeline, series.timestamps, series.values) >= 0.5
        if timeline.size < 2:
            return 0.0, 0.0
        duration = float(np.sum(np.diff(timeline) * active[:-1]))
        total = float(timeline[-1] - timeline[0])
        return duration, duration / total if total > 0.0 else 0.0
