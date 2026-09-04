from __future__ import annotations

import json
import struct
from collections.abc import Iterable, Mapping
from pathlib import Path
from typing import Any

import numpy as np
from mcap.reader import make_reader
from mcap.records import Schema
from mcap_protobuf.decoder import DecoderFactory

from jsb1_analysis.contracts import SignalCatalog, coerce_signal_catalog
from jsb1_analysis.telemetry import (
    SignalSeries,
    TelemetryDataset,
    TelemetryDatasetError,
)

from .run import RunData, canonical_signal_name


class McapLoadError(RuntimeError):
    """An MCAP artifact is missing, malformed, or cannot form logical signals."""


LEGACY_PROTOBUF_FIELDS = {
    "jsb.telemetry.v1.RollControlState": {
        "commanded_roll_rad": "commanded_roll",
        "commanded_roll_rate_rad_s": "commanded_roll_rate",
        "roll_error_rad": "roll_error",
        "roll_rad": "roll",
        "roll_rate_rad_s": "roll_rate",
        "roll_rate_error_rad_s": "roll_rate_error",
        "aileron_command": "aileron",
    }
}


def load_dataset(
    path: str | Path,
    *,
    signal_catalog: object | None = None,
    descriptor: bytes | None = None,
    variants: Iterable[str] = (),
) -> TelemetryDataset:
    """Decode MCAP once into immutable logical signals in contract wire units."""
    source = Path(path).expanduser()
    if not source.is_file():
        raise McapLoadError(f"MCAP file not found: {source}")
    catalog = coerce_signal_catalog(signal_catalog)
    samples: dict[tuple[str | None, str], list[tuple[int, float]]] = {}
    units: dict[str, str] = {}
    first_log_time: int | None = None
    decoder_factory = DecoderFactory()
    try:
        with source.open("rb") as stream:
            reader = make_reader(stream)
            for schema, channel, message in reader.iter_messages():
                first_log_time = (
                    message.log_time
                    if first_log_time is None
                    else min(first_log_time, message.log_time)
                )
                decoded = _decode_message(
                    schema,
                    channel.topic,
                    channel.message_encoding,
                    message.data,
                    catalog,
                    descriptor,
                    decoder_factory,
                )
                variant = (
                    catalog.variant_for_topic(channel.topic)
                    if catalog is not None
                    else _legacy_variant(channel.topic)
                )
                for logical_id, value in decoded.items():
                    samples.setdefault((variant, logical_id), []).append(
                        (message.log_time, value)
                    )
                    definition = (
                        catalog.by_logical_id().get(logical_id)
                        if catalog is not None
                        else None
                    )
                    units[logical_id] = definition.unit if definition else "raw"
    except McapLoadError:
        raise
    except Exception as exc:
        raise McapLoadError(f"could not parse {source.name}: {exc}") from exc
    if first_log_time is None or not samples:
        raise McapLoadError(f"no numeric telemetry found in {source.name}")

    definitions = catalog.by_logical_id() if catalog is not None else {}
    decoded_series: dict[tuple[str | None, str], SignalSeries] = {}
    try:
        for identity, points in samples.items():
            points.sort(key=lambda point: point[0])
            timestamps = np.asarray(
                [
                    (stamp - first_log_time) / 1_000_000_000
                    for stamp, _value in points
                ],
                dtype=np.float64,
            )
            values = np.asarray([value for _stamp, value in points], dtype=np.float64)
            decoded_series[identity] = SignalSeries(
                timestamps,
                values,
                units[identity[1]],
                definitions.get(identity[1]),
            )
    except TelemetryDatasetError as exc:
        raise McapLoadError(f"invalid telemetry in {source.name}: {exc}") from exc
    return TelemetryDataset(
        decoded_series,
        signal_catalog=catalog,
        contract_variants=tuple(variants),
        metadata={"source": str(source.resolve()), "format": "mcap"},
    )


def load_mcap(
    path: str | Path,
    signals: Iterable[str] | None = None,
    *,
    reference_signal: str | None = None,
    metadata: Mapping[str, object] | None = None,
    signal_catalog: object | None = None,
    descriptor: bytes | None = None,
    variant: str | None = None,
    variants: Iterable[str] = (),
) -> RunData:
    """Compatibility projection of :class:`TelemetryDataset` onto one variant."""
    dataset = load_dataset(
        path,
        signal_catalog=signal_catalog,
        descriptor=descriptor,
        variants=variants,
    )
    selected_variant = dataset.selected_variant(variant)
    requested = (
        [
            signal_id
            for item_variant, signal_id in dataset.series
            if item_variant == selected_variant
        ]
        if signals is None
        else list(dict.fromkeys(canonical_signal_name(item) for item in signals))
    )
    if reference_signal is not None:
        reference = canonical_signal_name(reference_signal)
        if reference not in requested:
            raise McapLoadError("reference_signal must be included in requested signals")
        requested.remove(reference)
        requested.insert(0, reference)
    if not requested:
        raise McapLoadError("at least one signal must be requested")
    try:
        timeline, aligned = dataset.align(requested, variant=variant)
    except KeyError as exc:
        missing = [
            item for item in requested if item not in dataset.available_signals(variant)
        ]
        raise McapLoadError(f"signals not found: {', '.join(missing)}") from exc
    except TelemetryDatasetError as exc:
        raise McapLoadError(str(exc)) from exc
    run_metadata = dict(metadata or {})
    run_metadata.update(dataset.metadata)
    run_metadata.update(
        {
            "reference_signal": requested[0],
            "alignment": "linear interpolation within common time range",
            "variant": selected_variant,
        }
    )
    return RunData(time=timeline, signals=aligned, metadata=run_metadata)


def _decode_message(
    schema: Schema | None,
    topic: str,
    encoding: str,
    data: bytes,
    catalog: SignalCatalog | None,
    descriptor: bytes | None,
    decoder_factory: DecoderFactory,
) -> dict[str, float]:
    if encoding.lower() == "protobuf":
        return _decode_protobuf(
            schema,
            topic,
            encoding,
            data,
            catalog,
            descriptor,
            decoder_factory,
        )
    wire = _decode_scalar_or_mapping(encoding, data)
    if catalog is None:
        return {
            canonical_signal_name(topic if name == "__value__" else name): value
            for name, value in wire.items()
        }
    mapping = catalog.field_mapping(topic=topic, message_name=None)
    if "__value__" in wire:
        candidates = [
            item.logical_id for item in catalog.signals if item.topic == topic
        ]
        return {candidates[0]: wire["__value__"]} if len(candidates) == 1 else {}
    return {
        mapping[field]: value
        for field, value in wire.items()
        if field in mapping
    }


def _decode_protobuf(
    schema: Schema | None,
    topic: str,
    encoding: str,
    data: bytes,
    catalog: SignalCatalog | None,
    descriptor: bytes | None,
    decoder_factory: DecoderFactory,
) -> dict[str, float]:
    if schema is None:
        return {}
    mapping = (
        catalog.field_mapping(topic=topic, message_name=schema.name)
        if catalog is not None
        else LEGACY_PROTOBUF_FIELDS.get(schema.name, {})
    )
    if not mapping:
        return {}
    decode_schema = (
        Schema(
            id=schema.id,
            data=descriptor,
            encoding=schema.encoding,
            name=schema.name,
        )
        if descriptor
        else schema
    )
    decoder = decoder_factory.decoder_for(encoding, decode_schema)
    if decoder is None:
        return {}
    payload = decoder(data)
    return {
        logical_id: float(getattr(payload, field))
        for field, logical_id in mapping.items()
    }


def _decode_scalar_or_mapping(encoding: str, data: bytes) -> dict[str, float]:
    normalized = encoding.lower()
    if normalized in {"json", "application/json"}:
        payload: Any = json.loads(data)
        if _numeric(payload):
            return {"__value__": float(payload)}
        if isinstance(payload, dict):
            if _numeric(payload.get("value")):
                return {"__value__": float(payload["value"])}
            return {
                str(name): float(value)
                for name, value in payload.items()
                if _numeric(value) and name not in {"time", "timestamp"}
            }
    if normalized in {"float64", "application/x-float64"} and len(data) == 8:
        return {"__value__": struct.unpack("<d", data)[0]}
    return {}


def _numeric(value: object) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _legacy_variant(topic: str) -> str | None:
    parts = [part for part in topic.strip("/").split("/") if part]
    if len(parts) >= 3 and parts[0] == "jsb" and parts[1] not in {
        "simulation",
        "event",
    }:
        return parts[1]
    return None
