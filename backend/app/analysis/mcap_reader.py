from __future__ import annotations

import json
import struct
from collections import OrderedDict
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
from mcap.reader import make_reader
from numpy.typing import NDArray


class McapReadError(RuntimeError):
    pass


ALIASES = {
    "roll_cmd": "commanded_roll",
    "cmd_roll": "commanded_roll",
    "roll_rate_cmd": "commanded_roll_rate",
    "cmd_roll_rate": "commanded_roll_rate",
}


@dataclass(frozen=True)
class SignalData:
    time: NDArray[np.float64]
    values: NDArray[np.float64]


def canonical_name(name: str) -> str:
    clean = name.strip("/").split("/")[-1]
    return ALIASES.get(clean, clean)


class McapRunReader:
    """MCAP boundary for numeric JSON or float64 telemetry channels.

    A message may be a JSON scalar/``{"value": number}`` on a signal topic, or a
    JSON object containing several named signals. Timestamps come from MCAP log time.
    Decoded files are retained in a small mtime-keyed in-memory cache.
    """

    def __init__(self, cache_entries: int = 4) -> None:
        self.cache_entries = cache_entries
        self._cache: OrderedDict[tuple[str, int, int], dict[str, SignalData]] = OrderedDict()

    def channels(self, path: Path) -> list[str]:
        return sorted(self._decode(path))

    def read_aligned(
        self,
        path: Path,
        channels: list[str],
        *,
        start: float | None = None,
        end: float | None = None,
    ) -> tuple[NDArray[np.float64], dict[str, NDArray[np.float64]]]:
        decoded = self._decode(path)
        requested = [canonical_name(name) for name in channels]
        missing = [name for name in requested if name not in decoded]
        if missing:
            raise McapReadError(f"channels not found: {', '.join(missing)}")
        reference = decoded[requested[0]].time
        mask = np.ones(len(reference), dtype=bool)
        if start is not None:
            mask &= reference >= start
        if end is not None:
            mask &= reference <= end
        timeline = reference[mask]
        if timeline.size == 0:
            return timeline, {name: np.array([], dtype=np.float64) for name in requested}
        result: dict[str, NDArray[np.float64]] = {}
        for name in requested:
            signal = decoded[name]
            if np.array_equal(signal.time, reference):
                result[name] = signal.values[mask]
            else:
                result[name] = np.interp(timeline, signal.time, signal.values)
        return timeline, result

    def _decode(self, path: Path) -> dict[str, SignalData]:
        if not path.is_file():
            raise McapReadError(f"MCAP file not found: {path.name}")
        stat = path.stat()
        key = (str(path.resolve()), stat.st_mtime_ns, stat.st_size)
        if key in self._cache:
            self._cache.move_to_end(key)
            return self._cache[key]
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
                    decoded_values = self._decode_message(
                        channel.topic, channel.message_encoding, message.data
                    )
                    for name, value in decoded_values.items():
                        samples.setdefault(canonical_name(name), []).append(
                            (message.log_time, float(value))
                        )
        except Exception as exc:
            raise McapReadError(f"could not parse {path.name}: {exc}") from exc
        if first_log_time is None or not samples:
            raise McapReadError(f"no numeric telemetry found in {path.name}")
        result: dict[str, SignalData] = {}
        for name, points in samples.items():
            points.sort(key=lambda item: item[0])
            times = np.asarray(
                [(stamp - first_log_time) / 1_000_000_000 for stamp, _ in points],
                dtype=np.float64,
            )
            values = np.asarray([value for _, value in points], dtype=np.float64)
            result[name] = SignalData(time=times, values=values)
        self._cache[key] = result
        while len(self._cache) > self.cache_entries:
            self._cache.popitem(last=False)
        return result

    @staticmethod
    def _decode_message(topic: str, encoding: str, data: bytes) -> dict[str, float]:
        if encoding.lower() in {"json", "application/json"}:
            payload: Any = json.loads(data)
            if isinstance(payload, (int, float)):
                return {topic: float(payload)}
            if isinstance(payload, dict):
                if isinstance(payload.get("value"), (int, float)):
                    return {topic: float(payload["value"])}
                return {
                    str(name): float(value)
                    for name, value in payload.items()
                    if isinstance(value, (int, float)) and name not in {"time", "timestamp"}
                }
        if encoding.lower() in {"float64", "application/x-float64"} and len(data) == 8:
            return {topic: struct.unpack("<d", data)[0]}
        return {}

