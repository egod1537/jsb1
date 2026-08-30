from __future__ import annotations

from pathlib import Path
from typing import Any, Literal

import numpy as np
import yaml
from numpy.typing import ArrayLike, NDArray
from pydantic import BaseModel, Field

from app.analysis.mcap_reader import McapRunReader


RAD_TO_DEG = 180.0 / np.pi
DEFAULT_SETTLING_BAND_DEG = 0.5
DEFAULT_RISE_TIME_LIMIT_SEC = 5.0
DEFAULT_SETTLING_TIME_LIMIT_SEC = 10.0
DEFAULT_OVERSHOOT_LIMIT_DEG = 1.0
DEFAULT_MAX_OSCILLATION_CYCLES = 2
DEFAULT_RESIDUAL_PP_LIMIT_DEG = 1.0
DEFAULT_SATURATION_TIME_LIMIT_SEC = 0.0
DEFAULT_STEADY_STATE_FRACTION = 0.2
WARN_RATIO = 1.25
RISE_LOW_FRACTION = 0.1
RISE_HIGH_FRACTION = 0.9
ROLL_HOLD_SIGNALS = (
    "commanded_roll",
    "roll",
    "commanded_roll_rate",
    "roll_rate",
    "aileron",
)


class AnalysisRegion(BaseModel):
    start_sec: float
    end_sec: float


class RollHoldAssessment(BaseModel):
    code: str
    severity: Literal["success", "warning", "info"]
    message: str
    start_sec: float | None = None
    end_sec: float | None = None


class RollHoldMarker(BaseModel):
    time_sec: float
    value: float | None = None
    label: str


class RollHoldCheck(BaseModel):
    id: str
    label: str
    category: Literal["tracking", "dynamics", "control"]
    status: Literal["pass", "warn", "fail", "unavailable"]
    actual: float | int | None
    target: float | int | None
    unit: str
    target_source: Literal["scenario", "default", "unavailable"]
    message: str
    start_sec: float | None = None
    end_sec: float | None = None


class RollHoldTarget(BaseModel):
    value: float | int | None
    unit: str
    source: Literal["scenario", "default", "unavailable"]


class RollHoldAnalysisResult(BaseModel):
    analyzer: Literal["roll_hold"] = "roll_hold"
    metrics: dict[str, float | int | bool | None]
    metric_units: dict[str, str]
    parameters: dict[str, float | int | str | None]
    targets: dict[str, RollHoldTarget]
    regions: dict[str, AnalysisRegion]
    intervals: dict[str, list[AnalysisRegion]]
    markers: dict[str, RollHoldMarker]
    checks: list[RollHoldCheck]
    assessment: list[RollHoldAssessment]
    missing_signals: list[str] = Field(default_factory=list)


class RollHoldAnalysisVariants(BaseModel):
    variants: dict[str, RollHoldAnalysisResult]


def _number(value: object) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    result = float(value)
    return result if np.isfinite(result) else None


def _mapping(value: object) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def _roll_command(definition: dict[str, Any]) -> tuple[dict[str, Any], str]:
    events = definition.get("events")
    if isinstance(events, list):
        for event in events:
            event_definition = _mapping(event)
            event_command = _mapping(event_definition.get("command"))
            if event_command.get("type") == "roll_hold":
                command = dict(event_command)
                command["start_sec"] = event_definition.get("time_sec")
                return command, "scenario_event"
    command = _mapping(definition.get("command"))
    return command, "legacy_command" if command else "telemetry"


def _array(value: ArrayLike | None) -> NDArray[np.float64] | None:
    if value is None:
        return None
    result = np.asarray(value, dtype=np.float64)
    return result if result.ndim == 1 else None


def _limit(
    definition: dict[str, Any],
    key: str,
    default: float,
    *,
    allow_zero: bool = True,
) -> tuple[float, Literal["scenario", "default"]]:
    value = _number(_mapping(definition.get("acceptance")).get(key))
    valid = value is not None and (value >= 0 if allow_zero else value > 0)
    return (value, "scenario") if valid else (default, "default")


def _metric_units() -> dict[str, str]:
    return {
        "rise_time_s": "s",
        "settling_time_s": "s",
        "overshoot_deg": "deg",
        "steady_state_error_deg": "deg",
        "rms_tracking_error_deg": "deg",
        "peak_roll_rate_deg_s": "deg/s",
        "oscillation_count": "cycles",
        "residual_oscillation_pp_deg": "deg",
        "dominant_oscillation_period_s": "s",
        "dominant_oscillation_frequency_hz": "Hz",
        "peak_aileron": "normalized",
        "rms_aileron": "normalized",
        "aileron_saturation_detected": "boolean",
        "aileron_saturation_time_s": "s",
        "aileron_saturation_fraction": "fraction",
    }


class RollHoldAnalyzer:
    """Deterministic step-response analysis for one frozen roll_hold scenario."""

    def __init__(self, reader: McapRunReader) -> None:
        self.reader = reader

    def analyze(
        self,
        scenario_path: Path,
        telemetry_path: Path,
        *,
        variant: str | None = None,
    ) -> RollHoldAnalysisResult:
        definition = yaml.safe_load(scenario_path.read_text(encoding="utf-8"))
        if not isinstance(definition, dict):
            raise ValueError("scenario snapshot root must be an object")
        available = set(self.reader.channels(telemetry_path, variant=variant))
        requested = [name for name in ROLL_HOLD_SIGNALS if name in available]
        if not requested:
            return self.analyze_series(definition, [], {})
        time, series = self.reader.read_aligned(
            telemetry_path, requested, variant=variant
        )
        return self.analyze_series(definition, time, series)

    def analyze_series(
        self,
        definition: dict[str, Any],
        time_sec: ArrayLike,
        series: dict[str, ArrayLike],
    ) -> RollHoldAnalysisResult:
        time = np.asarray(time_sec, dtype=np.float64)
        if time.ndim != 1 or (time.size > 1 and np.any(np.diff(time) < 0)):
            raise ValueError("telemetry time must be a monotonic one-dimensional array")
        arrays = {name: _array(series.get(name)) for name in ROLL_HOLD_SIGNALS}
        arrays = {
            name: values
            for name, values in arrays.items()
            if values is not None and len(values) == len(time)
        }
        missing = [name for name in ROLL_HOLD_SIGNALS if name not in arrays]

        command, command_source = _roll_command(definition)
        acceptance = _mapping(definition.get("acceptance"))
        initial_condition = _mapping(definition.get("initial_condition"))
        command_start = _number(command.get("start_sec"))
        if command_start is None:
            command_start = self._detect_command_start(time, arrays.get("commanded_roll"))
        command_start = command_start if command_start is not None else (float(time[0]) if time.size else 0.0)
        settling_band, settling_band_source = _limit(
            definition, "settling_band_deg", DEFAULT_SETTLING_BAND_DEG,
            allow_zero=False,
        )
        rise_limit, rise_limit_source = _limit(
            definition, "rise_time_limit_sec", DEFAULT_RISE_TIME_LIMIT_SEC,
            allow_zero=False,
        )
        settling_limit, settling_limit_source = _limit(
            definition, "settling_time_limit_sec", DEFAULT_SETTLING_TIME_LIMIT_SEC,
            allow_zero=False,
        )
        overshoot_limit, overshoot_limit_source = _limit(
            definition, "overshoot_limit_deg", DEFAULT_OVERSHOOT_LIMIT_DEG,
        )
        max_cycles, max_cycles_source = _limit(
            definition, "max_oscillation_cycles", DEFAULT_MAX_OSCILLATION_CYCLES,
        )
        residual_limit, residual_limit_source = _limit(
            definition, "residual_pp_limit_deg", DEFAULT_RESIDUAL_PP_LIMIT_DEG,
        )
        steady_limit = _number(acceptance.get("steady_state_error_limit_deg"))
        if steady_limit is not None and steady_limit >= 0:
            steady_limit_source: Literal["scenario", "default"] = "scenario"
        else:
            steady_limit = settling_band
            steady_limit_source = settling_band_source
        saturation_time_limit, saturation_time_limit_source = _limit(
            definition, "saturation_time_limit_sec",
            DEFAULT_SATURATION_TIME_LIMIT_SEC,
        )
        aileron_limit, aileron_limit_source = self._aileron_limit(definition)

        metrics: dict[str, float | int | bool | None] = {
            name: None for name in _metric_units()
        }
        regions: dict[str, AnalysisRegion] = {}
        intervals: dict[str, list[AnalysisRegion]] = {}
        markers: dict[str, RollHoldMarker] = {}
        if time.size:
            full_start, full_end = float(time[0]), float(time[-1])
            onset = int(np.searchsorted(time, command_start, side="left"))
            onset = min(onset, len(time) - 1)
            response_time = time[onset:]
            response_start = float(response_time[0])
            regions["pre_command"] = AnalysisRegion(start_sec=full_start, end_sec=response_start)
            regions["response"] = AnalysisRegion(start_sec=response_start, end_sec=full_end)
            initial_roll = self._initial_roll(initial_condition, arrays.get("roll"), onset)
            reference_deg = self._reference_deg(command, arrays.get("commanded_roll"), onset, len(time))
            target_deg = float(reference_deg[-1]) if reference_deg.size else _number(command.get("roll_deg"))
            markers["command"] = RollHoldMarker(
                time_sec=response_start,
                value=target_deg,
                label="Command",
            )
            roll = arrays.get("roll")

            settling_at: float | None = None
            if roll is not None and response_time.size and target_deg is not None:
                roll_deg = roll[onset:] * RAD_TO_DEG
                error_deg = reference_deg - roll_deg
                rise_time, rise_low_at, rise_high_at = self._rise_time_details(
                    response_time, roll_deg, initial_roll, target_deg
                )
                metrics["rise_time_s"] = rise_time
                rise_delta = target_deg - initial_roll
                if rise_low_at is not None:
                    markers["rise_10"] = RollHoldMarker(
                        time_sec=rise_low_at,
                        value=initial_roll + rise_delta * RISE_LOW_FRACTION,
                        label="Rise 10%",
                    )
                if rise_high_at is not None:
                    markers["rise_90"] = RollHoldMarker(
                        time_sec=rise_high_at,
                        value=initial_roll + rise_delta * RISE_HIGH_FRACTION,
                        label="Rise 90%",
                    )
                settling_time, settling_at = self._settling_time(
                    response_time, error_deg, settling_band
                )
                metrics["settling_time_s"] = settling_time
                direction = 1.0 if target_deg - initial_roll >= 0 else -1.0
                metrics["overshoot_deg"] = max(
                    0.0, float(np.max(direction * (roll_deg - target_deg)))
                )
                peak_roll_index = int(np.argmax(direction * roll_deg))
                markers["peak_roll"] = RollHoldMarker(
                    time_sec=float(response_time[peak_roll_index]),
                    value=float(roll_deg[peak_roll_index]),
                    label="Peak roll",
                )
                metrics["rms_tracking_error_deg"] = float(
                    np.sqrt(np.mean(np.square(error_deg)))
                )
                steady_count = max(
                    1, int(np.ceil(len(error_deg) * DEFAULT_STEADY_STATE_FRACTION))
                )
                metrics["steady_state_error_deg"] = float(
                    abs(np.mean(error_deg[-steady_count:]))
                )
                oscillation_count, frequency = self._oscillation_metrics(
                    response_time, roll_deg - reference_deg, settling_band
                )
                metrics["oscillation_count"] = oscillation_count
                metrics["dominant_oscillation_frequency_hz"] = frequency
                metrics["dominant_oscillation_period_s"] = (
                    1.0 / frequency if frequency is not None and frequency > 0 else None
                )
                steady_index = max(0, len(response_time) - steady_count)
                if settling_at is not None:
                    regions["settling"] = AnalysisRegion(
                        start_sec=response_start, end_sec=settling_at
                    )
                    markers["settled"] = RollHoldMarker(
                        time_sec=settling_at,
                        value=target_deg,
                        label="Settled",
                    )
                    steady_index = max(
                        steady_index,
                        int(np.searchsorted(response_time, settling_at, side="left")),
                    )
                residual_time = response_time[steady_index:]
                residual_roll = roll_deg[steady_index:]
                if residual_time.size:
                    regions["steady_state"] = AnalysisRegion(
                        start_sec=float(residual_time[0]), end_sec=full_end
                    )
                    residual_min_index = int(np.argmin(residual_roll))
                    residual_max_index = int(np.argmax(residual_roll))
                    metrics["residual_oscillation_pp_deg"] = float(
                        residual_roll[residual_max_index]
                        - residual_roll[residual_min_index]
                    )
                    markers["residual_min"] = RollHoldMarker(
                        time_sec=float(residual_time[residual_min_index]),
                        value=float(residual_roll[residual_min_index]),
                        label="Residual min",
                    )
                    markers["residual_max"] = RollHoldMarker(
                        time_sec=float(residual_time[residual_max_index]),
                        value=float(residual_roll[residual_max_index]),
                        label="Residual max",
                    )

            roll_rate = arrays.get("roll_rate")
            if roll_rate is not None and response_time.size:
                response_roll_rate = roll_rate[onset:] * RAD_TO_DEG
                peak_rate_index = int(np.argmax(np.abs(response_roll_rate)))
                metrics["peak_roll_rate_deg_s"] = float(abs(response_roll_rate[peak_rate_index]))
                markers["peak_roll_rate"] = RollHoldMarker(
                    time_sec=float(response_time[peak_rate_index]),
                    value=float(response_roll_rate[peak_rate_index]),
                    label="Peak roll rate",
                )
            aileron = arrays.get("aileron")
            if aileron is not None and response_time.size:
                response_aileron = aileron[onset:]
                peak_aileron_index = int(np.argmax(np.abs(response_aileron)))
                metrics["peak_aileron"] = float(abs(response_aileron[peak_aileron_index]))
                metrics["rms_aileron"] = float(
                    np.sqrt(np.mean(np.square(response_aileron)))
                )
                markers["peak_aileron"] = RollHoldMarker(
                    time_sec=float(response_time[peak_aileron_index]),
                    value=float(response_aileron[peak_aileron_index]),
                    label="Peak aileron",
                )
                metrics["aileron_saturation_detected"] = (
                    bool(np.any(np.abs(response_aileron) >= aileron_limit))
                    if aileron_limit is not None
                    else None
                )
                if aileron_limit is not None:
                    saturated = np.abs(response_aileron) >= aileron_limit
                    saturation_time = self._active_duration(response_time, saturated)
                    response_duration = float(response_time[-1] - response_time[0])
                    metrics["aileron_saturation_time_s"] = saturation_time
                    metrics["aileron_saturation_fraction"] = (
                        saturation_time / response_duration
                        if response_duration > 0 else 0.0
                    )
                    intervals["aileron_saturation"] = self._active_intervals(
                        response_time, saturated
                    )
                if metrics["aileron_saturation_detected"] is True and aileron_limit is not None:
                    saturation_index = int(
                        np.flatnonzero(np.abs(response_aileron) >= aileron_limit)[0]
                    )
                    markers["aileron_saturation"] = RollHoldMarker(
                        time_sec=float(response_time[saturation_index]),
                        value=float(response_aileron[saturation_index]),
                        label="Saturation",
                    )

        parameters: dict[str, float | int | str | None] = {
            "command_start_sec": command_start,
            "command_source": command_source,
            "settling_band_deg": settling_band,
            "settling_band_source": settling_band_source,
            "rise_time_limit_sec": rise_limit,
            "rise_time_limit_source": rise_limit_source,
            "settling_time_limit_sec": settling_limit,
            "settling_time_limit_source": settling_limit_source,
            "overshoot_limit_deg": overshoot_limit,
            "overshoot_limit_source": overshoot_limit_source,
            "max_oscillation_cycles": int(max_cycles),
            "max_oscillation_cycles_source": max_cycles_source,
            "residual_pp_limit_deg": residual_limit,
            "residual_pp_limit_source": residual_limit_source,
            "steady_state_error_limit_deg": steady_limit,
            "steady_state_error_limit_source": steady_limit_source,
            "saturation_time_limit_sec": saturation_time_limit,
            "saturation_time_limit_source": saturation_time_limit_source,
            "rise_low_fraction": RISE_LOW_FRACTION,
            "rise_high_fraction": RISE_HIGH_FRACTION,
            "steady_state_fraction": DEFAULT_STEADY_STATE_FRACTION,
            "warn_ratio": WARN_RATIO,
            "aileron_limit": aileron_limit,
            "aileron_limit_source": aileron_limit_source,
        }
        targets = self._targets(parameters, aileron_limit)
        checks = self._checks(metrics, targets, regions, missing)
        assessment = self._assess(checks, metrics, regions, missing)
        return RollHoldAnalysisResult(
            metrics=metrics,
            metric_units=_metric_units(),
            parameters=parameters,
            targets=targets,
            regions=regions,
            intervals=intervals,
            markers=markers,
            checks=checks,
            assessment=assessment,
            missing_signals=missing,
        )

    @staticmethod
    def _detect_command_start(
        time: NDArray[np.float64], command: NDArray[np.float64] | None
    ) -> float | None:
        if command is None or time.size == 0:
            return None
        changed = np.flatnonzero(np.abs(command - command[0]) > 1e-9)
        return float(time[changed[0]]) if changed.size else float(time[0])

    @staticmethod
    def _initial_roll(
        initial_condition: dict[str, Any],
        roll: NDArray[np.float64] | None,
        onset: int,
    ) -> float:
        configured = _number(initial_condition.get("roll_deg"))
        if configured is not None:
            return configured
        if roll is None or roll.size == 0:
            return 0.0
        sample = roll[: max(1, onset)]
        return float(np.mean(sample) * RAD_TO_DEG)

    @staticmethod
    def _reference_deg(
        command: dict[str, Any],
        commanded_roll: NDArray[np.float64] | None,
        onset: int,
        sample_count: int,
    ) -> NDArray[np.float64]:
        if commanded_roll is not None:
            return commanded_roll[onset:] * RAD_TO_DEG
        target = _number(command.get("roll_deg"))
        return np.full(max(0, sample_count - onset), target or 0.0, dtype=np.float64)

    @staticmethod
    def _rise_time(
        time: NDArray[np.float64],
        roll_deg: NDArray[np.float64],
        initial_deg: float,
        target_deg: float,
    ) -> float | None:
        return RollHoldAnalyzer._rise_time_details(
            time, roll_deg, initial_deg, target_deg
        )[0]

    @staticmethod
    def _rise_time_details(
        time: NDArray[np.float64],
        roll_deg: NDArray[np.float64],
        initial_deg: float,
        target_deg: float,
    ) -> tuple[float | None, float | None, float | None]:
        delta = target_deg - initial_deg
        if time.size == 0 or abs(delta) <= 1e-9:
            return None, None, None
        progress = np.sign(delta) * (roll_deg - initial_deg) / abs(delta)
        low = np.flatnonzero(progress >= RISE_LOW_FRACTION)
        if low.size == 0:
            return None, None, None
        high = np.flatnonzero(
            (np.arange(len(progress)) >= low[0]) & (progress >= RISE_HIGH_FRACTION)
        )
        low_at = float(time[low[0]])
        high_at = float(time[high[0]]) if high.size else None
        return high_at - low_at if high_at is not None else None, low_at, high_at

    @staticmethod
    def _settling_time(
        time: NDArray[np.float64],
        error_deg: NDArray[np.float64],
        band_deg: float,
    ) -> tuple[float | None, float | None]:
        if time.size == 0:
            return None, None
        within = np.abs(error_deg) <= band_deg
        remains_within = np.logical_and.accumulate(within[::-1])[::-1]
        candidates = np.flatnonzero(remains_within)
        if candidates.size == 0:
            return None, None
        settled_at = float(time[candidates[0]])
        return settled_at - float(time[0]), settled_at

    @staticmethod
    def _oscillation_metrics(
        time: NDArray[np.float64],
        deviation_deg: NDArray[np.float64],
        deadband_deg: float,
    ) -> tuple[int, float | None]:
        if time.size < 4:
            return 0, None
        signs = np.sign(deviation_deg)
        signs[np.abs(deviation_deg) <= deadband_deg] = 0
        nonzero = signs[signs != 0]
        crossings = int(np.count_nonzero(np.diff(nonzero) != 0)) if nonzero.size else 0
        cycles = crossings // 2
        if cycles == 0:
            return 0, None
        duration = float(time[-1] - time[0])
        if duration <= 0:
            return cycles, None
        spacing = duration / (len(time) - 1)
        uniform_time = np.linspace(float(time[0]), float(time[-1]), len(time))
        uniform = np.interp(uniform_time, time, deviation_deg)
        spectrum = np.abs(np.fft.rfft(uniform - np.mean(uniform)))
        frequencies = np.fft.rfftfreq(len(uniform), d=spacing)
        if len(frequencies) <= 1:
            return cycles, None
        index = int(np.argmax(spectrum[1:]) + 1)
        return cycles, float(frequencies[index]) if spectrum[index] > 0 else None

    @staticmethod
    def _active_duration(
        time: NDArray[np.float64], active: NDArray[np.bool_]
    ) -> float:
        if time.size < 2:
            return 0.0
        return float(np.sum(np.diff(time)[active[:-1]]))

    @staticmethod
    def _active_intervals(
        time: NDArray[np.float64], active: NDArray[np.bool_]
    ) -> list[AnalysisRegion]:
        if time.size < 2:
            return []
        active_intervals = active[:-1] & (np.diff(time) > 0)
        result: list[AnalysisRegion] = []
        start: int | None = None
        for index, enabled in enumerate(active_intervals):
            if enabled and start is None:
                start = index
            if start is not None and (not enabled or index == len(active_intervals) - 1):
                end = index if not enabled else index + 1
                result.append(AnalysisRegion(
                    start_sec=float(time[start]),
                    end_sec=float(time[end]),
                ))
                start = None
        return result

    @staticmethod
    def _aileron_limit(
        definition: dict[str, Any],
    ) -> tuple[float | None, Literal["scenario", "unavailable"]]:
        acceptance = _mapping(definition.get("acceptance"))
        control_limits = _mapping(definition.get("control_limits"))
        for candidate in (
            acceptance.get("aileron_limit"),
            acceptance.get("aileron_saturation_limit"),
            control_limits.get("aileron_abs_max"),
        ):
            value = _number(candidate)
            if value is not None and value > 0:
                return value, "scenario"
        return None, "unavailable"

    @staticmethod
    def _targets(
        parameters: dict[str, float | int | str | None],
        aileron_limit: float | None,
    ) -> dict[str, RollHoldTarget]:
        def configured(
            parameter: str, source: str, unit: str
        ) -> RollHoldTarget:
            value = parameters[parameter]
            return RollHoldTarget(
                value=value if isinstance(value, (int, float)) else None,
                unit=unit,
                source=source if source in {"scenario", "default"} else "unavailable",
            )

        def unavailable(unit: str) -> RollHoldTarget:
            return RollHoldTarget(value=None, unit=unit, source="unavailable")

        saturation_source = parameters["saturation_time_limit_source"]
        return {
            "rise_time_s": configured(
                "rise_time_limit_sec", str(parameters["rise_time_limit_source"]), "s"
            ),
            "settling_time_s": configured(
                "settling_time_limit_sec", str(parameters["settling_time_limit_source"]), "s"
            ),
            "overshoot_deg": configured(
                "overshoot_limit_deg", str(parameters["overshoot_limit_source"]), "deg"
            ),
            "steady_state_error_deg": configured(
                "steady_state_error_limit_deg",
                str(parameters["steady_state_error_limit_source"]), "deg",
            ),
            "rms_tracking_error_deg": unavailable("deg"),
            "oscillation_count": configured(
                "max_oscillation_cycles", str(parameters["max_oscillation_cycles_source"]),
                "cycles",
            ),
            "residual_oscillation_pp_deg": configured(
                "residual_pp_limit_deg", str(parameters["residual_pp_limit_source"]),
                "deg",
            ),
            "dominant_oscillation_period_s": unavailable("s"),
            "peak_aileron": RollHoldTarget(
                value=aileron_limit,
                unit="normalized",
                source="scenario" if aileron_limit is not None else "unavailable",
            ),
            "rms_aileron": unavailable("normalized"),
            "aileron_saturation_time_s": RollHoldTarget(
                value=(
                    parameters["saturation_time_limit_sec"]
                    if aileron_limit is not None else None
                ),
                unit="s",
                source=(
                    saturation_source
                    if aileron_limit is not None
                    and saturation_source in {"scenario", "default"}
                    else "unavailable"
                ),
            ),
        }

    @staticmethod
    def _checks(
        metrics: dict[str, float | int | bool | None],
        targets: dict[str, RollHoldTarget],
        regions: dict[str, AnalysisRegion],
        missing: list[str],
    ) -> list[RollHoldCheck]:
        response = regions.get("response")
        steady = regions.get("steady_state")

        definitions = [
            ("rise_time", "Rise Time", "tracking", "rise_time_s", response),
            ("settling_time", "Settling Time", "tracking", "settling_time_s", response),
            ("overshoot", "Overshoot", "tracking", "overshoot_deg", response),
            ("steady_state_error", "Steady-State Error", "tracking", "steady_state_error_deg", steady),
            ("rms_tracking_error", "RMS Tracking Error", "tracking", "rms_tracking_error_deg", response),
            ("oscillation", "Oscillation Cycles", "dynamics", "oscillation_count", response),
            ("residual_oscillation", "Residual P-P", "dynamics", "residual_oscillation_pp_deg", steady),
            ("dominant_period", "Dominant Period", "dynamics", "dominant_oscillation_period_s", response),
            ("peak_aileron", "Peak Aileron", "control", "peak_aileron", response),
            ("rms_aileron", "RMS Aileron", "control", "rms_aileron", response),
            ("saturation_time", "Saturation Time", "control", "aileron_saturation_time_s", response),
        ]
        checks: list[RollHoldCheck] = []
        for check_id, label, category, metric_name, region in definitions:
            actual = metrics[metric_name]
            target = targets[metric_name]
            status: Literal["pass", "warn", "fail", "unavailable"]
            if (
                check_id == "settling_time"
                and actual is None
                and "roll" not in missing
                and response is not None
            ):
                status = "fail"
                message = "Response did not settle before telemetry ended."
            elif (
                not isinstance(actual, (int, float))
                or not isinstance(target.value, (int, float))
            ):
                status = "unavailable"
                message = "Metric or target unavailable."
            elif actual <= target.value:
                status = "pass"
                message = "Within target."
            elif target.value <= 0:
                status = "fail"
                message = "Non-zero value exceeds the zero target."
            elif actual <= target.value * WARN_RATIO:
                status = "warn"
                message = f"Within {int((WARN_RATIO - 1) * 100)}% above target."
            else:
                status = "fail"
                message = "Target exceeded."
            checks.append(RollHoldCheck(
                id=check_id,
                label=label,
                category=category,
                status=status,
                actual=actual if isinstance(actual, (int, float)) else None,
                target=target.value,
                unit=target.unit,
                target_source=target.source,
                message=message,
                start_sec=region.start_sec if region else None,
                end_sec=region.end_sec if region else None,
            ))
        return checks

    @staticmethod
    def _assess(
        checks: list[RollHoldCheck],
        metrics: dict[str, float | int | bool | None],
        regions: dict[str, AnalysisRegion],
        missing: list[str],
    ) -> list[RollHoldAssessment]:
        results: list[RollHoldAssessment] = []
        if missing:
            results.append(RollHoldAssessment(
                code="missing_signals",
                severity="warning",
                message=f"Missing telemetry signals: {', '.join(missing)}",
            ))
        messages = {
            "rise_time": ("slow_rise", "Rise time exceeds the diagnostic target."),
            "settling_time": ("long_settling", "Long settling response detected."),
            "overshoot": ("excessive_overshoot", "Excessive roll overshoot detected."),
            "steady_state_error": ("large_steady_state_error", "Large steady-state tracking error detected."),
            "rms_tracking_error": ("large_rms_tracking_error", "Large RMS tracking error detected."),
            "oscillation": ("long_settling_oscillation", "Oscillation cycle count exceeds the diagnostic target."),
            "residual_oscillation": ("persistent_residual_oscillation", "Persistent residual oscillation detected in the steady-state region."),
            "dominant_period": ("long_dominant_period", "Dominant oscillation period exceeds the diagnostic target."),
            "peak_aileron": ("high_aileron_effort", "Peak aileron effort exceeds the configured limit."),
            "rms_aileron": ("high_rms_aileron_effort", "RMS aileron effort exceeds the diagnostic target."),
            "saturation_time": ("aileron_saturation", "Actuator saturation persisted for a non-zero duration."),
        }
        period = metrics["dominant_oscillation_period_s"]
        for check in checks:
            if check.status not in {"warn", "fail"}:
                continue
            code, message = messages[check.id]
            if check.id == "settling_time" and metrics["settling_time_s"] is None:
                code = "not_settled"
                message = "Roll response did not settle before telemetry ended."
            if check.id in {"oscillation", "residual_oscillation"} and isinstance(period, (int, float)):
                message += f" Dominant period is {period:.2f} s."
            results.append(RollHoldAssessment(
                code=code,
                severity="warning",
                message=message,
                start_sec=check.start_sec,
                end_sec=check.end_sec,
            ))
        if not results:
            results.append(RollHoldAssessment(
                code="within_limits",
                severity="success",
                message="All available Roll Hold diagnostic targets were met.",
            ))
        return results
