from __future__ import annotations

from datetime import datetime
from enum import Enum

from pydantic import BaseModel, ConfigDict, Field, field_validator


class DeploymentStatus(str, Enum):
    QUEUED = "queued"
    STARTING = "starting"
    RUNNING = "running"
    FAILED = "failed"
    STOPPED = "stopped"


class DeploymentCreate(BaseModel):
    repository_id: int = Field(gt=0)
    branch: str = Field(min_length=1, max_length=255)

    @field_validator("branch")
    @classmethod
    def strip_branch(cls, value: str) -> str:
        if value != value.strip():
            raise ValueError("branch must not have surrounding whitespace")
        return value


class BranchDeployment(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    repository_id: int
    branch: str
    commit_sha: str
    slug: str
    hostname: str
    status: DeploymentStatus
    frontend_port: int | None = None
    backend_port: int | None = None
    compose_project: str
    worktree_path: str
    created_at: datetime
    started_at: datetime | None = None
    stopped_at: datetime | None = None
    updated_at: datetime
    error_message: str | None = None
