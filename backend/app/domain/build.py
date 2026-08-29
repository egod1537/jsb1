from __future__ import annotations

from datetime import datetime
from enum import StrEnum

from pydantic import BaseModel, ConfigDict, Field


class BuildStatus(StrEnum):
    QUEUED = "queued"
    RUNNING = "running"
    COMPLETED = "completed"
    FAILED = "failed"


class BuildCreate(BaseModel):
    repository_id: int = Field(ge=1)
    revision: str = Field(min_length=1, max_length=255)
    rebuild: bool = False


class Build(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    repository_id: int
    repository_name: str | None = None
    commit_sha: str
    branch: str | None = None
    status: BuildStatus
    build_dir: str
    executable_path: str | None = None
    stdout_path: str
    stderr_path: str
    created_at: datetime
    started_at: datetime | None = None
    completed_at: datetime | None = None
    error_message: str | None = None
    reused: bool = False


class InstanceStatus(StrEnum):
    QUEUED = "queued"
    RUNNING = "running"
    STOPPED = "stopped"
    FAILED = "failed"


class Instance(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    build_id: int
    run_id: int | None = None
    pid: int | None = None
    status: InstanceStatus
    started_at: datetime | None = None
    stopped_at: datetime | None = None
