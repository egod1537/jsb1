from __future__ import annotations

from dataclasses import dataclass
from pathlib import PurePosixPath
from typing import Protocol


class InvalidScenarioObjectId(ValueError):
    pass


@dataclass(frozen=True)
class ScenarioObject:
    id: str
    size: int | None = None
    modified_at: float | None = None


class ScenarioSource(Protocol):
    def list_objects(self) -> list[ScenarioObject]: ...
    def fetch(self, object_id: str) -> bytes: ...
    def stat(self, object_id: str) -> ScenarioObject: ...


def validate_object_id(object_id: str) -> PurePosixPath:
    if not object_id or "\x00" in object_id or "\\" in object_id:
        raise InvalidScenarioObjectId("invalid scenario object id")
    path = PurePosixPath(object_id)
    if path.is_absolute() or ".." in path.parts or path.suffix.lower() not in {".yaml", ".yml"}:
        raise InvalidScenarioObjectId("scenario object must be a relative YAML path")
    normalized = PurePosixPath(*[part for part in path.parts if part not in {"", "."}])
    if not normalized.parts:
        raise InvalidScenarioObjectId("invalid scenario object id")
    return normalized
