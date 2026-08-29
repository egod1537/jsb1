from __future__ import annotations

from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field, field_validator


class RepositoryCreate(BaseModel):
    name: str = Field(min_length=1, max_length=100)
    remote_url: str = Field(min_length=1, max_length=2048)
    local_path: str = Field(min_length=1, max_length=255)
    default_branch: str | None = Field(default=None, min_length=1, max_length=255)

    @field_validator("name")
    @classmethod
    def validate_name(cls, value: str) -> str:
        if not value.replace("-", "").replace("_", "").isalnum():
            raise ValueError("name may contain only letters, numbers, '-' and '_'")
        return value


class Repository(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    name: str
    remote_url: str
    local_path: str
    default_branch: str
    created_at: datetime
    updated_at: datetime
    last_fetched_at: datetime | None = None


class RepositoryStatus(Repository):
    current_branch: str | None = None
    head_commit: str
    dirty: bool
    status: str = "ready"


class Branch(BaseModel):
    name: str
    commit_sha: str
    current: bool = False
    remote: bool = False


class Revision(BaseModel):
    repository_id: int
    commit_sha: str
    branch: str | None = None
    commit_message: str
    committed_at: datetime
    dirty: bool
