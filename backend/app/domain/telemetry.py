from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class RuntimeSignalDefinition:
    id: str
    topic: str
    field: str
    type: str
    unit: str
    frame: str
    group: str | None
    description: str | None
    axis: str | None = None
    sign: str | None = None
    convention: str | None = None
    value_range: tuple[float | None, float | None] | None = None
    required: bool = False

    @property
    def api_id(self) -> str:
        return self.id.rsplit(".", 1)[-1]


@dataclass(frozen=True)
class RuntimeSignalCatalog:
    contract_version: str
    telemetry_schema_version: int
    topics: dict[str, dict[str, Any]]
    signals: tuple[RuntimeSignalDefinition, ...]

    def by_api_id(self) -> dict[str, RuntimeSignalDefinition]:
        return {self.api_id_for(item): item for item in self.signals}

    def api_id_for(self, item: RuntimeSignalDefinition) -> str:
        leaf = item.api_id
        namespace = item.id.rsplit(".", 1)[0]
        if namespace.startswith("tecs"):
            return item.id
        if namespace in {"course", "pitch", "pitch_rate"} and leaf in {
            "commanded",
            "actual",
            "error",
        }:
            return item.id
        return (
            item.id
            if sum(candidate.api_id == leaf for candidate in self.signals) > 1
            else leaf
        )

    def field_mapping(self, message_name: str | None = None) -> dict[str, str]:
        allowed_topics = {
            topic
            for topic, metadata in self.topics.items()
            if message_name is None or metadata.get("message") == message_name
        }
        return {
            item.field: self.api_id_for(item)
            for item in self.signals
            if message_name is None or item.topic in allowed_topics
        }
