from __future__ import annotations

from collections.abc import Mapping
from dataclasses import asdict, dataclass

import numpy as np
from numpy.typing import NDArray

from jsb1_analysis.telemetry import MissingSignalError, TelemetryDataset


@dataclass(frozen=True)
class PitchHoldConfig:
    commanded_pitch_signal: str = "pitch.commanded"
    pitch_signal: str = "pitch.actual"
    pitch_rate_signal: str = "pitch_rate.actual"
    elevator_signal: str = "elevator"
    positive_saturation_signal: str = "saturation_positive"
    negative_saturation_signal: str = "saturation_negative"
    integrator_limited_signal: str = "integrator_limited"
    settling_band_rad: float = np.deg2rad(0.2)
    tail_window_sec: float = 2.0


@dataclass(frozen=True)
class PitchHoldResult:
    steady_state_mean_pitch_error_rad: float
    steady_state_max_abs_pitch_error_rad: float
    steady_state_rms_pitch_error_rad: float
    steady_state_max_abs_pitch_rate_rad_s: float
    steady_state_rms_pitch_rate_rad_s: float
    overshoot_rad: float
    settling_time_s: float | None
    oscillation_count: int
    pitch_peak_to_peak_rad: float
    max_abs_elevator: float
    elevator_saturation_duration_s: float | None
    elevator_saturation_ratio: float | None
    integrator_limit_duration_s: float | None
    integrator_limit_ratio: float | None
    tail_window_s: float

    def as_dict(self) -> dict[str, float | int | None]:
        return asdict(self)


class PitchHoldAnalyzer:
    scenario_type = "pitch_hold"
    required_signals = frozenset(
        {"pitch.commanded", "pitch.actual", "pitch_rate.actual", "elevator"}
    )

    def __init__(self, config: PitchHoldConfig | None = None) -> None:
        self.config = config or PitchHoldConfig()

    def analyze_dataset(
        self,
        dataset: TelemetryDataset,
        *,
        variant: str | None = None,
        start_time: float | None = None,
        end_time: float | None = None,
        scenario_definition: object | None = None,
        **_context: object,
    ) -> PitchHoldResult:
        names = [
            self.config.commanded_pitch_signal,
            self.config.pitch_signal,
            self.config.pitch_rate_signal,
            self.config.elevator_signal,
        ]
        time, values = dataset.align(
            names, variant=variant, start=start_time, end=end_time
        )
        commanded = values[self.config.commanded_pitch_signal]
        pitch = values[self.config.pitch_signal]
        pitch_rate = values[self.config.pitch_rate_signal]
        elevator = values[self.config.elevator_signal]
        error = commanded - pitch
        onset = self._last_command_onset(commanded)
        tail = time >= max(float(time[onset]), float(time[-1] - self.config.tail_window_sec))
        tail_error = error[tail]
        tail_rate = pitch_rate[tail]
        settling_band = self._settling_band(scenario_definition)
        response_time = time[onset:]
        response_error = error[onset:]
        response_pitch = pitch[onset:]
        saturation = self._combined_optional_flags(
            dataset,
            (
                self.config.positive_saturation_signal,
                self.config.negative_saturation_signal,
            ),
            response_time,
            variant,
        )
        integrator = self._optional_flag(
            dataset,
            self.config.integrator_limited_signal,
            response_time,
            variant,
        )
        saturation_duration, saturation_ratio = self._duration_ratio(
            response_time, saturation
        )
        integrator_duration, integrator_ratio = self._duration_ratio(
            response_time, integrator
        )
        return PitchHoldResult(
            steady_state_mean_pitch_error_rad=float(np.mean(tail_error)),
            steady_state_max_abs_pitch_error_rad=float(np.max(np.abs(tail_error))),
            steady_state_rms_pitch_error_rad=float(
                np.sqrt(np.mean(np.square(tail_error)))
            ),
            steady_state_max_abs_pitch_rate_rad_s=float(
                np.max(np.abs(tail_rate))
            ),
            steady_state_rms_pitch_rate_rad_s=float(
                np.sqrt(np.mean(np.square(tail_rate)))
            ),
            overshoot_rad=self._overshoot(commanded, pitch, onset),
            settling_time_s=self._settling_time(
                time, error, onset, settling_band
            ),
            oscillation_count=self._oscillation_count(
                response_error, settling_band
            ),
            pitch_peak_to_peak_rad=float(np.ptp(response_pitch)),
            max_abs_elevator=float(np.max(np.abs(elevator[onset:]))),
            elevator_saturation_duration_s=saturation_duration,
            elevator_saturation_ratio=saturation_ratio,
            integrator_limit_duration_s=integrator_duration,
            integrator_limit_ratio=integrator_ratio,
            tail_window_s=float(time[-1] - time[tail][0]) if tail_error.size > 1 else 0.0,
        )

    @staticmethod
    def _last_command_onset(commanded: NDArray[np.float64]) -> int:
        changes = np.flatnonzero(np.abs(np.diff(commanded)) > 1.0e-9)
        return int(changes[-1] + 1) if changes.size else 0

    @staticmethod
    def _settling_time(
        time: NDArray[np.float64],
        error: NDArray[np.float64],
        onset: int,
        band: float,
    ) -> float | None:
        outside = np.flatnonzero(np.abs(error[onset:]) > band)
        if outside.size == 0:
            return 0.0
        candidate = onset + int(outside[-1]) + 1
        if candidate >= time.size:
            return None
        return float(time[candidate] - time[onset])

    @staticmethod
    def _overshoot(
        commanded: NDArray[np.float64], pitch: NDArray[np.float64], onset: int
    ) -> float:
        baseline = float(pitch[max(0, onset - 1)])
        target = float(commanded[-1])
        step = target - baseline
        if abs(step) <= 1.0e-12:
            return 0.0
        excess = np.sign(step) * (pitch[onset:] - target)
        return float(max(0.0, np.max(excess)))

    @staticmethod
    def _oscillation_count(error: NDArray[np.float64], deadband: float) -> int:
        signs = np.sign(np.where(np.abs(error) <= deadband, 0.0, error))
        signs = signs[signs != 0.0]
        return (
            int(np.count_nonzero(signs[1:] != signs[:-1]) // 2)
            if signs.size >= 2
            else 0
        )

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
    def _optional_flag(
        dataset: TelemetryDataset,
        signal: str,
        timeline: NDArray[np.float64],
        variant: str | None,
    ) -> NDArray[np.bool_] | None:
        try:
            series = dataset.signal(signal, variant)
        except MissingSignalError:
            return None
        return np.interp(timeline, series.timestamps, series.values) >= 0.5

    def _combined_optional_flags(
        self,
        dataset: TelemetryDataset,
        signals: tuple[str, ...],
        timeline: NDArray[np.float64],
        variant: str | None,
    ) -> NDArray[np.bool_] | None:
        values = [
            self._optional_flag(dataset, signal, timeline, variant)
            for signal in signals
        ]
        available = [value for value in values if value is not None]
        if not available:
            return None
        return np.logical_or.reduce(available)

    @staticmethod
    def _duration_ratio(
        timeline: NDArray[np.float64], active: NDArray[np.bool_] | None
    ) -> tuple[float | None, float | None]:
        if active is None:
            return None, None
        if timeline.size < 2:
            return 0.0, 0.0
        duration = float(np.sum(np.diff(timeline) * active[:-1]))
        total = float(timeline[-1] - timeline[0])
        return duration, duration / total if total > 0.0 else 0.0

