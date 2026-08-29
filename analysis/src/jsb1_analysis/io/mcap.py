from __future__ import annotations

import json
import struct
from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
from mcap.reader import make_reader
from numpy.typing import NDArray

from .run import RunData, canonical_signal_name


class McapLoadError(RuntimeError):
    """An MCAP artifact is missing, malformed, or cannot form aligned signals."""


@dataclass(frozen=True)
class _SignalSeries:
    time: NDArray[np.float64]
    values: NDArray[np.float64]


def _numeric(value: object) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _decode_message(topic: str, encoding: str, data: bytes) -> dict[str, float]:
    normalized_encoding = encoding.lower()
    if normalized_encoding in {"json", "application/json"}:
        payload: Any = json.loads(data)
        if _numeric(payload):
            return {topic: float(payload)}
        if isinstance(payload, dict):
            if _numeric(payload.get("value")):
                return {topic: float(payload["value"])}
            return {
                str(name): float(value)
                for name, value in payload.items()
                if _numeric(value) and name not in {"time", "timestamp"}
            }
    if normalized_encoding in {"float64", "application/x-float64"} and len(data) == 8:
        return {topic: struct.unpack("<d", data)[0]}
    return {}


def _decode(path: Path) -> dict[str, _SignalSeries]:
    if not path.is_file():
        raise McapLoadError(f"MCAP file not found: {path}")
    samples: dict[str, list[tuple[int, float]]] = {}
    first_log_time: int | None = None
    try:
        with path.open("rb") as stream:
            reader = make_reader(stream)
            for _schema, channel, message in reader.iter_messages():
                first_log_time = (
                    message.log_time
                    if first_log_time is None
                    else min(first_log_time, message.log_time)
                )
                for raw_name, value in _decode_message(
                    channel.topic, channel.message_encoding, message.data
                ).items():
                    name = canonical_signal_name(raw_name)
                    samples.setdefault(name, []).append((message.log_time, value))
    except McapLoadError:
        raise
    except Exception as error:
        raise McapLoadError(f"could not parse {path.name}: {error}") from error
    if first_log_time is None or not samples:
        raise McapLoadError(f"no numeric telemetry found in {path.name}")

    decoded: dict[str, _SignalSeries] = {}
    for name, points in samples.items():
        points.sort(key=lambda point: point[0])
        time = np.asarray(
            [(stamp - first_log_time) / 1_000_000_000 for stamp, _value in points],
            dtype=np.float64,
        )
        values = np.asarray([value for _stamp, value in points], dtype=np.float64)
        if not np.all(np.isfinite(values)):
            raise McapLoadError(f"signal {name!r} contains NaN or infinite values")
        if time.size > 1 and np.any(np.diff(time) <= 0):
            raise McapLoadError(
                f"signal {name!r} timestamps are not strictly increasing"
            )
        decoded[name] = _SignalSeries(time=time, values=values)
    return decoded


def load_mcap(
    path: str | Path,
    signals: Iterable[str] | None = None,
    *,
    reference_signal: str | None = None,
    metadata: Mapping[str, object] | None = None,
) -> RunData:
    """Decode a JSB1 MCAP artifact into one validated :class:`RunData`.

    The wire contract matches the backend reader: numeric JSON or little-endian
    float64 messages, canonical aliases, and MCAP log time relative to the first
    message. When channels have unequal timelines, the first requested channel
    (or ``reference_signal``) supplies the output timeline. It is cropped to the
    common time range and other channels are linearly interpolated exactly once.
    """

    source = Path(path).expanduser()
    decoded = _decode(source)
    if signals is None:
        requested = list(decoded)
    else:
        requested = list(dict.fromkeys(canonical_signal_name(name) for name in signals))
    if not requested:
        raise McapLoadError("at least one signal must be requested")
    missing = [name for name in requested if name not in decoded]
    if missing:
        raise McapLoadError(f"signals not found: {', '.join(missing)}")

    if reference_signal is not None:
        reference = canonical_signal_name(reference_signal)
        if reference not in requested:
            raise McapLoadError(
                "reference_signal must be included in requested signals"
            )
        requested.remove(reference)
        requested.insert(0, reference)
    else:
        reference = requested[0]

    lower_bound = max(float(decoded[name].time[0]) for name in requested)
    upper_bound = min(float(decoded[name].time[-1]) for name in requested)
    reference_time = decoded[reference].time
    mask = (reference_time >= lower_bound) & (reference_time <= upper_bound)
    timeline = reference_time[mask]
    if timeline.size == 0:
        raise McapLoadError("requested signals do not share a common time range")

    aligned: dict[str, NDArray[np.float64]] = {}
    for name in requested:
        signal = decoded[name]
        if np.array_equal(signal.time, reference_time):
            aligned[name] = signal.values[mask]
        else:
            aligned[name] = np.interp(timeline, signal.time, signal.values)

    run_metadata = dict(metadata or {})
    run_metadata.update(
        {
            "source": str(source.resolve()),
            "format": "mcap",
            "reference_signal": reference,
            "alignment": "linear interpolation within common time range",
        }
    )
    return RunData(time=timeline, signals=aligned, metadata=run_metadata)
