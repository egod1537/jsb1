from __future__ import annotations

from datetime import datetime
from enum import StrEnum

from pydantic import BaseModel, ConfigDict, Field

from app.domain.identifiers import BuildId, CommitSha, RepositoryId, RunId
from app.domain.pipeline import PipelineStage


class BuildStatus(StrEnum):
    QUEUED = "queued"
    RUNNING = "running"
    COMPLETED = "completed"
    FAILED = "failed"


BUILD_STATUS_TRANSITIONS: dict[BuildStatus, frozenset[BuildStatus]] = {
    BuildStatus.QUEUED: frozenset({BuildStatus.RUNNING, BuildStatus.FAILED}),
    BuildStatus.RUNNING: frozenset({BuildStatus.COMPLETED, BuildStatus.FAILED}),
    BuildStatus.COMPLETED: frozenset(),
    BuildStatus.FAILED: frozenset(),
}


class BuildCreate(BaseModel):
    repository_id: RepositoryId = Field(ge=1)
    revision: str = Field(min_length=1, max_length=255)
    rebuild: bool = False


class Build(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: BuildId
    repository_id: RepositoryId
    repository_name: str | None = None
    commit_sha: CommitSha
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
    current_stage: str | None = None
    stages: list[PipelineStage] = Field(default_factory=list)


class InstanceStatus(StrEnum):
    QUEUED = "queued"
    RUNNING = "running"
    STOPPED = "stopped"
    FAILED = "failed"


INSTANCE_STATUS_TRANSITIONS: dict[InstanceStatus, frozenset[InstanceStatus]] = {
    InstanceStatus.QUEUED: frozenset({InstanceStatus.RUNNING, InstanceStatus.FAILED}),
    InstanceStatus.RUNNING: frozenset({InstanceStatus.STOPPED, InstanceStatus.FAILED}),
    InstanceStatus.STOPPED: frozenset(),
    InstanceStatus.FAILED: frozenset(),
}


class Instance(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    build_id: BuildId
    run_id: RunId | None = None
    pid: int | None = None
    status: InstanceStatus
    started_at: datetime | None = None
    stopped_at: datetime | None = None
