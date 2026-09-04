from __future__ import annotations

import re
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from types import MappingProxyType
from typing import Any

import yaml


class TelemetryContractError(ValueError):
    """Telemetry contract is missing, unsupported, or internally inconsistent."""


class UnsupportedTelemetryContract(TelemetryContractError):
    pass


@dataclass(frozen=True)
class SignalDefinition:
    id: str
    topic: str
    field: str
    type: str
    unit: str
    frame: str
    group: str | None = None
    description: str | None = None
    axis: str | None = None
    sign: str | None = None
    convention: str | None = None
    value_range: tuple[float | None, float | None] | None = None
    required: bool = False

    @property
    def logical_id(self) -> str:
        return self.id.rsplit(".", 1)[-1]


@dataclass(frozen=True)
class SignalCatalog:
    contract_version: str
    telemetry_schema_version: int
    topics: Mapping[str, Mapping[str, object]]
    signals: tuple[SignalDefinition, ...]

    def __post_init__(self) -> None:
        version = re.fullmatch(
            r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)",
            self.contract_version,
        )
        if version is None:
            raise TelemetryContractError(
                f"invalid telemetry contract version: {self.contract_version}"
            )
        if int(version.group(1)) not in {1, 2}:
            raise UnsupportedTelemetryContract(
                f"unsupported telemetry contract version: {self.contract_version}"
            )
        logical_ids = [item.logical_id for item in self.signals]
        if len(logical_ids) != len(set(logical_ids)):
            raise TelemetryContractError("logical signal ids must be unique")
        missing_topics = sorted({item.topic for item in self.signals} - set(self.topics))
        if missing_topics:
            raise TelemetryContractError(
                "signals reference undeclared topics: " + ", ".join(missing_topics)
            )
        object.__setattr__(
            self,
            "topics",
            MappingProxyType(
                {
                    topic: MappingProxyType(dict(metadata))
                    for topic, metadata in self.topics.items()
                }
            ),
        )

    def by_logical_id(self) -> dict[str, SignalDefinition]:
        return {item.logical_id: item for item in self.signals}

    def variant_for_topic(self, topic: str) -> str | None:
        metadata = self.topics.get(topic, {})
        source = metadata.get("source")
        return source if isinstance(source, str) and source != "runtime" else None

    def field_mapping(
        self,
        *,
        topic: str,
        message_name: str | None,
    ) -> dict[str, str]:
        metadata = self.topics.get(topic, {})
        topic_message = metadata.get("message")
        if message_name is not None and topic_message != message_name:
            return {}
        compatible_topics = {
            name
            for name, value in self.topics.items()
            if message_name is not None and value.get("message") == message_name
        }
        return {
            item.field: item.logical_id
            for item in self.signals
            if item.topic == topic or item.topic in compatible_topics
        }

    @classmethod
    def from_mapping(cls, payload: Mapping[str, object]) -> SignalCatalog:
        try:
            contract_version = str(payload["contract_version"])
            schema_version = int(payload["telemetry_schema_version"])
            raw_topics = payload["topics"]
            raw_signals = payload["signals"]
        except (KeyError, TypeError, ValueError) as exc:
            raise TelemetryContractError("invalid signal catalog header") from exc
        if not isinstance(raw_topics, Mapping) or not isinstance(raw_signals, Mapping):
            raise TelemetryContractError("signal catalog topics/signals must be mappings")
        topics: dict[str, Mapping[str, object]] = {}
        for topic, metadata in raw_topics.items():
            if not isinstance(topic, str) or not isinstance(metadata, Mapping):
                raise TelemetryContractError("invalid signal catalog topic")
            topics[topic] = dict(metadata)
        signals: list[SignalDefinition] = []
        for signal_id, raw in raw_signals.items():
            if not isinstance(signal_id, str) or not isinstance(raw, Mapping):
                raise TelemetryContractError("invalid signal catalog entry")
            try:
                raw_range = raw.get("range")
                value_range = (
                    (raw_range[0], raw_range[1])
                    if isinstance(raw_range, list) and len(raw_range) == 2
                    else None
                )
                signals.append(
                    SignalDefinition(
                        id=signal_id,
                        topic=str(raw["topic"]),
                        field=str(raw["field"]),
                        type=str(raw["type"]),
                        unit=str(raw["unit"]),
                        frame=str(raw["frame"]),
                        group=_optional_string(raw.get("group")),
                        description=_optional_string(raw.get("description")),
                        axis=_optional_string(raw.get("axis")),
                        sign=_optional_string(raw.get("sign")),
                        convention=_optional_string(raw.get("convention")),
                        value_range=value_range,
                        required=bool(raw.get("required", False)),
                    )
                )
            except KeyError as exc:
                raise TelemetryContractError(
                    f"signal {signal_id} is missing required metadata"
                ) from exc
        return cls(contract_version, schema_version, topics, tuple(signals))

    @classmethod
    def load(cls, path: str | Path) -> SignalCatalog:
        try:
            payload = yaml.safe_load(Path(path).read_text(encoding="utf-8"))
        except (OSError, UnicodeError, yaml.YAMLError) as exc:
            raise TelemetryContractError("could not read signal catalog") from exc
        if not isinstance(payload, Mapping):
            raise TelemetryContractError("signal catalog root must be a mapping")
        return cls.from_mapping(payload)


def coerce_signal_catalog(value: object | None) -> SignalCatalog | None:
    if value is None or isinstance(value, SignalCatalog):
        return value
    topics = getattr(value, "topics", None)
    signals = getattr(value, "signals", None)
    if (
        not isinstance(topics, Mapping)
        or not isinstance(signals, Sequence)
        or isinstance(signals, (str, bytes))
    ):
        raise TelemetryContractError("unsupported signal catalog object")
    definitions = tuple(
        SignalDefinition(
            id=item.id,
            topic=item.topic,
            field=item.field,
            type=item.type,
            unit=item.unit,
            frame=item.frame,
            group=item.group,
            description=item.description,
            axis=item.axis,
            sign=item.sign,
            convention=item.convention,
            value_range=item.value_range,
            required=item.required,
        )
        for item in signals
    )
    return SignalCatalog(
        str(value.contract_version),
        int(value.telemetry_schema_version),
        topics,
        definitions,
    )


def _optional_string(value: Any) -> str | None:
    return value if isinstance(value, str) else None
