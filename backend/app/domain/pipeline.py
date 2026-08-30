from __future__ import annotations

from datetime import datetime
from enum import StrEnum

from pydantic import BaseModel


class PipelineStageStatus(StrEnum):
    PENDING = "pending"
    RUNNING = "running"
    SUCCESS = "success"
    FAILED = "failed"
    SKIPPED = "skipped"


class PipelineStage(BaseModel):
    id: str
    label: str
    status: PipelineStageStatus = PipelineStageStatus.PENDING
    started_at: datetime | None = None
    finished_at: datetime | None = None
    duration_sec: float | None = None
    message: str | None = None
    error: str | None = None
