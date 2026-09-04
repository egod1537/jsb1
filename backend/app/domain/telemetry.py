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
        return {item.api_id: item for item in self.signals}

    def field_mapping(self, message_name: str | None = None) -> dict[str, str]:
        allowed_topics = {
            topic
            for topic, metadata in self.topics.items()
            if message_name is None or metadata.get("message") == message_name
        }
        return {
            item.field: item.api_id
            for item in self.signals
            if message_name is None or item.topic in allowed_topics
        }
