from __future__ import annotations

from collections.abc import Mapping
from dataclasses import asdict, dataclass

import numpy as np
from numpy.typing import NDArray

from jsb1_analysis.telemetry import MissingSignalError, TelemetryDataset


@dataclass(frozen=True)
class TecsConfig:
    tail_window_sec: float = 10.0
    altitude_settling_band_m: float = 2.0
    airspeed_settling_band_mps: float = 0.3


@dataclass(frozen=True)
class TecsResult:
    steady_state_mean_altitude_error_m: float
    steady_state_max_abs_altitude_error_m: float
    steady_state_rms_altitude_error_m: float
    altitude_overshoot_m: float
    altitude_settling_time_s: float | None
    steady_state_mean_airspeed_error_mps: float
    steady_state_max_abs_airspeed_error_mps: float
    steady_state_rms_airspeed_error_mps: float
    minimum_airspeed_mps: float
    maximum_airspeed_mps: float
    airspeed_settling_time_s: float | None
    pitch_target_peak_to_peak_rad: float
    throttle_peak_to_peak: float
    throttle_saturation_duration_s: float | None
    throttle_saturation_ratio: float | None
    elevator_saturation_duration_s: float | None
    elevator_saturation_ratio: float | None
    underspeed_protection_activated: bool | None
    overspeed_protection_activated: bool | None
    safe_airspeed_violation_duration_s: float | None
    tail_window_s: float

    def as_dict(self) -> dict[str, float | bool | None]:
        return asdict(self)


class TecsAnalyzer:
    scenario_type = "tecs"
    required_signals = frozenset({
        "tecs.altitude.target", "tecs.altitude.actual",
        "tecs.airspeed.target", "tecs.airspeed.actual",
        "tecs.pitch_target", "tecs.throttle_target",
    })

    def __init__(self, config: TecsConfig | None = None) -> None:
        self.config = config or TecsConfig()

    def analyze_dataset(self, dataset: TelemetryDataset, *, variant: str | None = None,
        start_time: float | None = None, end_time: float | None = None,
        scenario_definition: object | None = None,
        minimum_safe_airspeed_mps: float | None = None,
        maximum_safe_airspeed_mps: float | None = None, **_context: object) -> TecsResult:
        names = list(self.required_signals)
        time, values = dataset.align(names, variant=variant, start=start_time, end=end_time)
        altitude_target = values["tecs.altitude.target"]
        altitude = values["tecs.altitude.actual"]
        airspeed_target = values["tecs.airspeed.target"]
        airspeed = values["tecs.airspeed.actual"]
        altitude_error = altitude_target - altitude
        airspeed_error = airspeed_target - airspeed
        onset = self._last_command_onset(altitude_target, airspeed_target)
        tail = time >= max(float(time[onset]), float(time[-1] - self.config.tail_window_sec))
        altitude_band, airspeed_band = self._bands(scenario_definition)
        response_time = time[onset:]
        throttle_saturation = self._flags(dataset, (
            "tecs.throttle_upper_saturated", "tecs.throttle_lower_saturated"),
            response_time, variant)
        elevator_saturation = self._flags(dataset, (
            "saturation_positive", "saturation_negative"), response_time, variant)
        throttle_duration, throttle_ratio = self._duration_ratio(response_time, throttle_saturation)
        elevator_duration, elevator_ratio = self._duration_ratio(response_time, elevator_saturation)
        safe_violation = None
        if minimum_safe_airspeed_mps is not None or maximum_safe_airspeed_mps is not None:
            active = np.zeros(response_time.size, dtype=bool)
            response_airspeed = airspeed[onset:]
            if minimum_safe_airspeed_mps is not None:
                active |= response_airspeed < minimum_safe_airspeed_mps
            if maximum_safe_airspeed_mps is not None:
                active |= response_airspeed > maximum_safe_airspeed_mps
            safe_violation, _ = self._duration_ratio(response_time, active)
        return TecsResult(
            steady_state_mean_altitude_error_m=float(np.mean(altitude_error[tail])),
            steady_state_max_abs_altitude_error_m=float(np.max(np.abs(altitude_error[tail]))),
            steady_state_rms_altitude_error_m=float(np.sqrt(np.mean(np.square(altitude_error[tail])))),
            altitude_overshoot_m=self._overshoot(altitude_target, altitude, onset),
            altitude_settling_time_s=self._settling(time, altitude_error, onset, altitude_band),
            steady_state_mean_airspeed_error_mps=float(np.mean(airspeed_error[tail])),
            steady_state_max_abs_airspeed_error_mps=float(np.max(np.abs(airspeed_error[tail]))),
            steady_state_rms_airspeed_error_mps=float(np.sqrt(np.mean(np.square(airspeed_error[tail])))),
            minimum_airspeed_mps=float(np.min(airspeed[onset:])),
            maximum_airspeed_mps=float(np.max(airspeed[onset:])),
            airspeed_settling_time_s=self._settling(time, airspeed_error, onset, airspeed_band),
            pitch_target_peak_to_peak_rad=float(
                np.ptp(values["tecs.pitch_target"][onset:])
            ),
            throttle_peak_to_peak=float(
                np.ptp(values["tecs.throttle_target"][onset:])
            ),
            throttle_saturation_duration_s=throttle_duration,
            throttle_saturation_ratio=throttle_ratio,
            elevator_saturation_duration_s=elevator_duration,
            elevator_saturation_ratio=elevator_ratio,
            underspeed_protection_activated=self._activated(dataset, "tecs.underspeed_active", variant),
            overspeed_protection_activated=self._activated(dataset, "tecs.overspeed_active", variant),
            safe_airspeed_violation_duration_s=safe_violation,
            tail_window_s=float(time[-1] - time[tail][0]) if np.count_nonzero(tail) > 1 else 0.0,
        )

    @staticmethod
    def _last_command_onset(altitude: NDArray[np.float64], airspeed: NDArray[np.float64]) -> int:
        changes = np.flatnonzero((np.abs(np.diff(altitude)) > 1e-9) | (np.abs(np.diff(airspeed)) > 1e-9))
        return int(changes[-1] + 1) if changes.size else 0

    def _bands(self, definition: object | None) -> tuple[float, float]:
        if not isinstance(definition, Mapping) or not isinstance(definition.get("acceptance"), Mapping):
            return self.config.altitude_settling_band_m, self.config.airspeed_settling_band_mps
        acceptance = definition["acceptance"]
        altitude = acceptance.get("altitude_error_limit_m", self.config.altitude_settling_band_m)
        airspeed = acceptance.get("airspeed_error_limit_mps", self.config.airspeed_settling_band_mps)
        return float(altitude), float(airspeed)

    @staticmethod
    def _settling(time: NDArray[np.float64], error: NDArray[np.float64], onset: int, band: float) -> float | None:
        outside = np.flatnonzero(np.abs(error[onset:]) > band)
        if outside.size == 0: return 0.0
        candidate = onset + int(outside[-1]) + 1
        return None if candidate >= time.size else float(time[candidate] - time[onset])

    @staticmethod
    def _overshoot(target: NDArray[np.float64], actual: NDArray[np.float64], onset: int) -> float:
        step = float(target[-1] - actual[max(0, onset - 1)])
        return 0.0 if abs(step) <= 1e-12 else float(max(0.0, np.max(np.sign(step) * (actual[onset:] - target[-1]))))

    @staticmethod
    def _flags(dataset: TelemetryDataset, names: tuple[str, ...], time: NDArray[np.float64], variant: str | None) -> NDArray[np.bool_] | None:
        flags = []
        for name in names:
            try: series = dataset.signal(name, variant)
            except MissingSignalError: continue
            flags.append(np.interp(time, series.timestamps, series.values) >= .5)
        return np.logical_or.reduce(flags) if flags else None

    @staticmethod
    def _duration_ratio(time: NDArray[np.float64], active: NDArray[np.bool_] | None) -> tuple[float | None, float | None]:
        if active is None: return None, None
        if time.size < 2: return 0.0, 0.0
        duration = float(np.sum(np.diff(time) * active[:-1])); total = float(time[-1] - time[0])
        return duration, duration / total if total > 0 else 0.0

    @staticmethod
    def _activated(dataset: TelemetryDataset, name: str, variant: str | None) -> bool | None:
        try: return bool(np.any(dataset.signal(name, variant).values >= .5))
        except MissingSignalError: return None
