from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from pathlib import PurePosixPath
from typing import Protocol


class InvalidScenarioObjectId(ValueError):
    pass


class ScenarioSourceType(StrEnum):
    """Semantic source classes independent of their transport names."""

    BUNDLED = "bundled"
    MANAGED = "managed"
    REMOTE = "remote"


@dataclass(frozen=True)
class ScenarioObject:
    id: str
    size: int | None = None
    modified_at: float | None = None


class ScenarioSource(Protocol):
    """Read-only source port. Implementations own I/O, never YAML semantics."""

    @property
    def source_type(self) -> ScenarioSourceType: ...

    def list(self) -> list[ScenarioObject]: ...

    def read(self, object_id: str) -> bytes: ...


def validate_object_id(object_id: str) -> PurePosixPath:
    if not object_id or "\x00" in object_id or "\\" in object_id:
        raise InvalidScenarioObjectId("invalid scenario object id")
    path = PurePosixPath(object_id)
    if (
        path.is_absolute()
        or ".." in path.parts
        or path.suffix.lower() not in {".yaml", ".yml"}
    ):
        raise InvalidScenarioObjectId(
            "scenario object must be a relative YAML path"
        )
    normalized = PurePosixPath(
        *[part for part in path.parts if part not in {"", "."}]
    )
    if not normalized.parts:
        raise InvalidScenarioObjectId("invalid scenario object id")
    return normalized
