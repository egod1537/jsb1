from __future__ import annotations

from datetime import datetime
from enum import StrEnum

from pydantic import BaseModel, ConfigDict, Field, field_validator

from app.domain.build import Instance


class RunStatus(StrEnum):
    QUEUED = "queued"
    RUNNING = "running"
    COMPLETED = "completed"
    FAILED = "failed"


class RunCreate(BaseModel):
    scenario: str = Field(min_length=1, max_length=255)
    autopilot: str = Field(min_length=1, max_length=64)
    commit_sha: str | None = Field(default=None, min_length=1, max_length=64)
    build_id: int | None = Field(default=None, ge=1)

    @field_validator("autopilot")
    @classmethod
    def validate_autopilot(cls, value: str) -> str:
        if not value.replace("-", "").replace("_", "").isalnum():
            raise ValueError("autopilot may contain only letters, numbers, '-' and '_'")
        return value

    @field_validator("commit_sha")
    @classmethod
    def validate_commit(cls, value: str | None) -> str | None:
        if value is None:
            return None
        if not all(character in "0123456789abcdefABCDEF" for character in value):
            raise ValueError("commit_sha must be hexadecimal")
        return value.lower()


class Metric(BaseModel):
    name: str
    value: float | None
    unit: str


class Artifact(BaseModel):
    id: int
    run_id: int
    kind: str
    filename: str
    download_url: str


class Run(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    status: RunStatus
    repository_id: int | None = None
    repository_name: str | None = None
    build_id: int | None = None
    build_branch: str | None = None
    commit_sha: str | None
    scenario_name: str
    scenario_path: str
    autopilot: str
    created_at: datetime
    started_at: datetime | None = None
    finished_at: datetime | None = None
    exit_code: int | None = None
    simulation_time_sec: float | None = None
    wall_time_sec: float | None = None
    output_directory: str | None = None
    error_message: str | None = None


class RunSummary(BaseModel):
    id: int
    status: RunStatus
    repository_id: int | None = None
    repository_name: str | None = None
    build_id: int | None = None
    build_branch: str | None = None
    commit_sha: str | None
    scenario_name: str
    autopilot: str
    created_at: datetime
    wall_time_sec: float | None = None


class RunDetail(BaseModel):
    run: Run
    metrics: list[Metric]
    artifacts: list[Artifact]
    instance: Instance | None = None


class SignalResponse(BaseModel):
    time: list[float]
    series: dict[str, list[float]]
    units: dict[str, str]
    source_points: int
    returned_points: int
