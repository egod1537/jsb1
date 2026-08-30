from __future__ import annotations

import json
import struct
from collections import OrderedDict
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
from mcap.reader import make_reader
from mcap.records import Schema
from mcap_protobuf.decoder import DecoderFactory
from numpy.typing import NDArray


class McapReadError(RuntimeError):
    pass


ALIASES = {
    "roll_cmd": "commanded_roll",
    "cmd_roll": "commanded_roll",
    "roll_rate_cmd": "commanded_roll_rate",
    "cmd_roll_rate": "commanded_roll_rate",
}

PROTOBUF_SIGNAL_FIELDS = {
    "jsb.telemetry.v1.RollControlState": {
        "commanded_roll_rad": "commanded_roll",
        "commanded_roll_rate_rad_s": "commanded_roll_rate",
        "roll_error_rad": "roll_error",
        "roll_rad": "roll",
        "roll_rate_rad_s": "roll_rate",
        "roll_rate_error_rad_s": "roll_rate_error",
        "aileron_command": "aileron",
    },
}


@dataclass(frozen=True)
class SignalData:
    time: NDArray[np.float64]
    values: NDArray[np.float64]


def canonical_name(name: str) -> str:
    clean = name.strip("/").split("/")[-1]
    return ALIASES.get(clean, clean)


def variant_from_topic(topic: str) -> str | None:
    parts = [part for part in topic.strip("/").split("/") if part]
    if len(parts) >= 3 and parts[0] == "jsb":
        candidate = parts[1]
        if candidate not in {"simulation", "event"}:
            return candidate
    return None


class McapRunReader:
    """MCAP boundary for JSB0 Protobuf, numeric JSON, or float64 telemetry.

    A message may be a JSON scalar/``{"value": number}`` on a signal topic, or a
    JSON object containing several named signals. JSB0 RollControlState Protobuf
    messages are decoded from the FileDescriptorSet embedded in the MCAP schema.
    Timestamps come from MCAP log time. Decoded files are retained in a small
    mtime-keyed in-memory cache.
    """

    def __init__(self, cache_entries: int = 4) -> None:
        self.cache_entries = cache_entries
        self._cache: OrderedDict[
            tuple[str, int, int], dict[tuple[str | None, str], SignalData]
        ] = OrderedDict()

    def variants(self, path: Path) -> list[str]:
        return sorted({variant for variant, _ in self._decode(path) if variant})

    def channels(self, path: Path, variant: str | None = None) -> list[str]:
        decoded = self._decode(path)
        named_variants = {item_variant for item_variant, _ in decoded if item_variant}
        selected = variant
        if selected is not None and selected not in named_variants and not named_variants:
            selected = None
        return sorted({
            name for item_variant, name in decoded
            if item_variant == selected
            or (variant is None and (not named_variants or item_variant is not None))
        })

    def read_aligned(
        self,
        path: Path,
        channels: list[str],
        *,
        variant: str | None = None,
        start: float | None = None,
        end: float | None = None,
    ) -> tuple[NDArray[np.float64], dict[str, NDArray[np.float64]]]:
        decoded = self._decode(path)
        requested = [canonical_name(name) for name in channels]
        named_variants = {item_variant for item_variant, _ in decoded if item_variant}
        selected_variant = variant
        if selected_variant is None and named_variants:
            selected_variant = (
                "primary" if "primary" in named_variants
                else sorted(named_variants)[0]
            )
        if selected_variant is not None and selected_variant not in named_variants and not named_variants:
            selected_variant = None
        missing = [
            name for name in requested
            if (selected_variant, name) not in decoded
        ]
        if missing:
            raise McapReadError(f"channels not found: {', '.join(missing)}")
        reference = decoded[(selected_variant, requested[0])].time
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
            signal = decoded[(selected_variant, name)]
            if np.array_equal(signal.time, reference):
                result[name] = signal.values[mask]
            else:
                result[name] = np.interp(timeline, signal.time, signal.values)
        return timeline, result

    def _decode(self, path: Path) -> dict[tuple[str | None, str], SignalData]:
        if not path.is_file():
            raise McapReadError(f"MCAP file not found: {path.name}")
        stat = path.stat()
        key = (str(path.resolve()), stat.st_mtime_ns, stat.st_size)
        if key in self._cache:
            self._cache.move_to_end(key)
            return self._cache[key]
        samples: dict[tuple[str | None, str], list[tuple[int, float]]] = {}
        first_log_time: int | None = None
        protobuf_decoder = DecoderFactory()
        try:
            with path.open("rb") as stream:
                reader = make_reader(stream)
                for schema, channel, message in reader.iter_messages():
                    first_log_time = (
                        message.log_time
                        if first_log_time is None
                        else min(first_log_time, message.log_time)
                    )
                    if channel.message_encoding.lower() == "protobuf":
                        decoded_values = self._decode_protobuf_message(
                            schema,
                            channel.message_encoding,
                            message.data,
                            protobuf_decoder,
                        )
                    else:
                        decoded_values = self._decode_message(
                            channel.topic, channel.message_encoding, message.data
                        )
                    for name, value in decoded_values.items():
                        identity = (
                            variant_from_topic(channel.topic),
                            canonical_name(name),
                        )
                        samples.setdefault(identity, []).append(
                            (message.log_time, float(value))
                        )
        except Exception as exc:
            raise McapReadError(f"could not parse {path.name}: {exc}") from exc
        if first_log_time is None or not samples:
            raise McapReadError(f"no numeric telemetry found in {path.name}")
        result: dict[tuple[str | None, str], SignalData] = {}
        for identity, points in samples.items():
            points.sort(key=lambda item: item[0])
            times = np.asarray(
                [(stamp - first_log_time) / 1_000_000_000 for stamp, _ in points],
                dtype=np.float64,
            )
            values = np.asarray([value for _, value in points], dtype=np.float64)
            result[identity] = SignalData(time=times, values=values)
        self._cache[key] = result
        while len(self._cache) > self.cache_entries:
            self._cache.popitem(last=False)
        return result

    @staticmethod
    def _decode_protobuf_message(
        schema: Schema | None,
        encoding: str,
        data: bytes,
        decoder_factory: DecoderFactory,
    ) -> dict[str, float]:
        if schema is None:
            return {}
        field_mapping = PROTOBUF_SIGNAL_FIELDS.get(schema.name)
        if field_mapping is None:
            return {}
        decoder = decoder_factory.decoder_for(encoding, schema)
        if decoder is None:
            return {}
        payload = decoder(data)
        return {
            signal_name: float(getattr(payload, field_name))
            for field_name, signal_name in field_mapping.items()
        }

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
