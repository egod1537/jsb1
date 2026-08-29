from __future__ import annotations

from pydantic import BaseModel, ConfigDict


class BuildInfo(BaseModel):
    model_config = ConfigDict(frozen=True)

    branch: str
    commit: str
    short_commit: str
    built_at: str
    hostname: str | None = None
