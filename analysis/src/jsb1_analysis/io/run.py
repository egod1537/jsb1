from __future__ import annotations

import json
from collections.abc import Mapping
from dataclasses import dataclass, field
from pathlib import Path
from types import MappingProxyType

import numpy as np
import pandas as pd
from numpy.typing import ArrayLike, NDArray

SIGNAL_ALIASES = {
    "roll_cmd": "commanded_roll",
    "cmd_roll": "commanded_roll",
    "roll_rate_cmd": "commanded_roll_rate",
    "cmd_roll_rate": "commanded_roll_rate",
}


class RunDataError(ValueError):
    """The run cannot be represented as validated, aligned signal arrays."""


class SignalNotFoundError(KeyError):
    """A requested canonical signal is absent from the run."""


def canonical_signal_name(name: str) -> str:
    clean = name.strip("/").split("/")[-1]
    return SIGNAL_ALIASES.get(clean, clean)


def _validated_array(value: ArrayLike, *, label: str) -> NDArray[np.float64]:
    array = np.asarray(value, dtype=np.float64)
    if array.ndim != 1:
        raise RunDataError(f"{label} must be a one-dimensional array")
    if not np.all(np.isfinite(array)):
        raise RunDataError(f"{label} contains NaN or infinite values")
    result = array.copy()
    result.flags.writeable = False
    return result


@dataclass(frozen=True)
class RunData:
    """One decoded run with a shared time base and canonical signal names.

    Arrays are validated and copied on construction, then marked read-only so the
    same instance can be safely reused by notebooks and multiple analyzers.
    """

    time: ArrayLike
    signals: Mapping[str, ArrayLike]
    metadata: Mapping[str, object] = field(default_factory=dict)

    def __post_init__(self) -> None:
        timeline = _validated_array(self.time, label="time")
        if timeline.size == 0:
            raise RunDataError("time array is empty")
        if timeline.size > 1 and np.any(np.diff(timeline) <= 0):
            raise RunDataError("time must be strictly increasing")

        validated: dict[str, NDArray[np.float64]] = {}
        for raw_name, raw_values in self.signals.items():
            name = canonical_signal_name(raw_name)
            if not name:
                raise RunDataError("signal names must not be empty")
            if name in validated:
                raise RunDataError(f"duplicate canonical signal: {name}")
            values = _validated_array(raw_values, label=f"signal {name!r}")
            if values.size != timeline.size:
                raise RunDataError(
                    f"signal {name!r} has {values.size} samples; expected {timeline.size}"
                )
            validated[name] = values
        if not validated:
            raise RunDataError("run contains no signals")

        object.__setattr__(self, "time", timeline)
        object.__setattr__(self, "signals", MappingProxyType(validated))
        object.__setattr__(self, "metadata", MappingProxyType(dict(self.metadata)))

    def signal(self, name: str) -> NDArray[np.float64]:
        canonical = canonical_signal_name(name)
        try:
            return self.signals[canonical]
        except KeyError as error:
            available = ", ".join(self.available_signals())
            raise SignalNotFoundError(
                f"signal {canonical!r} is not available; available signals: {available}"
            ) from error

    def available_signals(self) -> tuple[str, ...]:
        return tuple(sorted(self.signals))

    def slice(self, start: float | None = None, end: float | None = None) -> RunData:
        if start is not None and end is not None and start > end:
            raise RunDataError("slice start must be less than or equal to end")
        mask = np.ones(self.time.size, dtype=bool)
        if start is not None:
            mask &= self.time >= start
        if end is not None:
            mask &= self.time <= end
        if not np.any(mask):
            raise RunDataError(f"time slice [{start}, {end}] contains no samples")
        metadata = dict(self.metadata)
        metadata["slice"] = {"start": start, "end": end}
        return RunData(
            time=self.time[mask],
            signals={name: values[mask] for name, values in self.signals.items()},
            metadata=metadata,
        )

    def to_dataframe(self) -> pd.DataFrame:
        """Return a new DataFrame with ``time`` followed by every signal column."""

        return pd.DataFrame({"time": self.time, **self.signals})


def load_run(path: str | Path) -> RunData:
    """Load a ``telemetry.mcap`` file or a JSB1 ``data/runs/<id>`` directory."""

    from .mcap import load_mcap

    source = Path(path).expanduser()
    if source.is_file():
        return load_mcap(source)
    if not source.is_dir():
        raise FileNotFoundError(f"run path does not exist: {source}")
    telemetry = source / "telemetry.mcap"
    if not telemetry.is_file():
        raise FileNotFoundError(f"run directory has no telemetry.mcap: {source}")
    metadata: dict[str, object] = {"run_directory": str(source.resolve())}
    manifest = source / "run.json"
    if manifest.is_file():
        try:
            decoded = json.loads(manifest.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise RunDataError(f"could not parse run manifest: {manifest}") from error
        if not isinstance(decoded, dict):
            raise RunDataError(f"run manifest must contain a JSON object: {manifest}")
        metadata["run"] = decoded
    return load_mcap(telemetry, metadata=metadata)
