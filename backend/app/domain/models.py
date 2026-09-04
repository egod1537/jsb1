from __future__ import annotations

import math
from datetime import datetime
from enum import StrEnum

from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator

from app.domain.build import Instance
from app.domain.identifiers import (
    BuildId,
    CommitSha,
    ComparisonId,
    RepositoryId,
    RunId,
    ScenarioId,
)
from app.domain.pipeline import PipelineStage


class RunStatus(StrEnum):
    QUEUED = "queued"
    RUNNING = "running"
    COMPLETED = "completed"
    FAILED = "failed"


RUN_STATUS_TRANSITIONS: dict[RunStatus, frozenset[RunStatus]] = {
    RunStatus.QUEUED: frozenset({RunStatus.RUNNING, RunStatus.FAILED}),
    RunStatus.RUNNING: frozenset({RunStatus.COMPLETED, RunStatus.FAILED}),
    RunStatus.COMPLETED: frozenset(),
    RunStatus.FAILED: frozenset(),
}


class ComparisonStatus(StrEnum):
    QUEUED = "queued"
    RUNNING = "running"
    COMPLETED = "completed"
    FAILED = "failed"
    PARTIAL_FAILED = "partial_failed"


class RunCreate(BaseModel):
    scenario: str = Field(min_length=1, max_length=1024)
    scenario_source: str = Field(default="bundled", pattern="^(bundled|sftp|managed)$")
    autopilot: str | None = Field(
        default=None,
        min_length=1,
        max_length=64,
        description="Deprecated alias of variant.",
    )
    variant: str | None = Field(default=None, min_length=1, max_length=64)
    repository_id: RepositoryId | None = Field(default=None, ge=1)
    branch: str | None = Field(default=None, min_length=1, max_length=255)
    commit_sha: CommitSha | None = Field(default=None, min_length=1, max_length=64)
    build_id: BuildId | None = Field(default=None, ge=1)
    controller_parameters: dict[str, float] = Field(default_factory=dict)

    @model_validator(mode="after")
    def validate_revision_source(self) -> RunCreate:
        if self.repository_id is not None and self.branch is None:
            raise ValueError("repository_id requires branch")
        if self.branch is not None and (
            self.commit_sha is not None or self.build_id is not None
        ):
            raise ValueError("branch-based runs cannot specify commit_sha or build_id")
        if self.scenario_source in {"sftp", "managed"} and self.branch is None:
            raise ValueError(
                "Remote and managed scenarios require branch-based immutable validation"
            )
        return self

    @field_validator("autopilot", "variant")
    @classmethod
    def validate_autopilot(cls, value: str | None) -> str | None:
        if value is None:
            return None
        if not value.replace("-", "").replace("_", "").isalnum():
            raise ValueError("variant may contain only letters, numbers, '-' and '_'")
        return value

    @model_validator(mode="after")
    def validate_variant_alias(self) -> RunCreate:
        if (
            self.variant is not None
            and self.autopilot is not None
            and self.variant != self.autopilot
        ):
            raise ValueError("autopilot and variant conflict")
        return self

    @field_validator("controller_parameters")
    @classmethod
    def validate_controller_parameters(
        cls, values: dict[str, float]
    ) -> dict[str, float]:
        normalized: dict[str, float] = {}
        for parameter_id, value in values.items():
            if not parameter_id or not parameter_id.replace("_", "").isalnum():
                raise ValueError(
                    "controller parameter ids may contain only letters, numbers, and '_'"
                )
            if isinstance(value, bool):
                # Pydantic field validators must report invalid input as ValueError.
                raise ValueError("controller parameter values must be numeric")  # noqa: TRY004
            number = float(value)
            if not math.isfinite(number):
                raise ValueError("controller parameter values must be finite")
            normalized[parameter_id] = number
        return normalized

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
    run_id: RunId
    kind: str
    filename: str
    download_url: str
    sha256: str | None = None
    size_bytes: int | None = None


class Run(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: RunId
    status: RunStatus
    repository_id: RepositoryId | None = None
    repository_name: str | None = None
    branch: str | None = None
    build_id: BuildId | None = None
    build_branch: str | None = None
    commit_sha: CommitSha | None
    scenario_name: str
    scenario_type: str | None = None
    scenario_path: str
    scenario_id: ScenarioId | None = None
    scenario_source: str | None = None
    scenario_sha256: str | None = None
    parameter_snapshot_path: str | None = None
    parameter_snapshot_sha256: str | None = None
    contract_version: str | None = None
    autopilot: str
    execution_variant: str
    execution_mode: str = "single"
    variants: list[str] = Field(default_factory=list)
    variant_results: dict[str, dict[str, object]] = Field(default_factory=dict)
    variant_parameters: dict[str, dict[str, float]] = Field(default_factory=dict)
    comparison_id: ComparisonId | None = None
    controller_parameters: dict[str, float] = Field(default_factory=dict)
    controller_parameter_overrides: dict[str, float] = Field(default_factory=dict)
    created_at: datetime
    started_at: datetime | None = None
    finished_at: datetime | None = None
    exit_code: int | None = None
    simulation_time_sec: float | None = None
    wall_time_sec: float | None = None
    output_directory: str | None = None
    error_message: str | None = None
    current_stage: str | None = None
    stages: list[PipelineStage] = Field(default_factory=list)


class RunSummary(BaseModel):
    id: RunId
    status: RunStatus
    repository_id: RepositoryId | None = None
    repository_name: str | None = None
    branch: str | None = None
    build_id: BuildId | None = None
    build_branch: str | None = None
    commit_sha: CommitSha | None
    scenario_name: str
    scenario_type: str | None = None
    scenario_id: ScenarioId | None = None
    scenario_source: str | None = None
    scenario_sha256: str | None = None
    autopilot: str
    execution_variant: str
    execution_mode: str = "single"
    variants: list[str] = Field(default_factory=list)
    comparison_id: ComparisonId | None = None
    created_at: datetime
    wall_time_sec: float | None = None
    current_stage: str | None = None


class RuntimeParameterDefinition(BaseModel):
    """Typed projection of one JSB0 execution parameter definition."""

    model_config = ConfigDict(frozen=True, extra="allow")

    id: str
    display_name: str
    description: str | None = None
    module: str | None = None
    controller: str | None = None
    type: str = "number"
    unit: str | None = None
    minimum: float | None = None
    maximum: float | None = None
    increment: float | None = None
    variants: list[str] = Field(default_factory=list)
    aircraft: list[str] = Field(default_factory=list)
    algorithm_default: float | None = None
    default_value: float
    profiles: dict[str, dict[str, float]] = Field(default_factory=dict)
    read_only: bool = False
    experimental: bool = False

    # Presentation-only fields retained for older JSB1 clients. They are not
    # used as Runtime semantics by the backend.
    category: str | None = None
    group: str | None = None
    symbol: str | None = None
    step: float | None = None


class ControllerParameterDefinition(RuntimeParameterDefinition):
    """Backward-compatible API name for RuntimeParameterDefinition."""


class RuntimeControllerParametersResponse(BaseModel):
    branch: str
    commit_sha: CommitSha
    source: str
    transport: str
    parameters: list[RuntimeParameterDefinition]


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


class SignalMetadata(BaseModel):
    name: str
    unit: str
    display_name: str | None = None
    symbol: str | None = None
    symbol_latex: str | None = None
    category: str | None = None
    subcategory: str | None = None
    contract_id: str | None = None
    topic: str | None = None
    field: str | None = None
    source_unit: str | None = None
    frame: str | None = None
    axis: str | None = None
    sign: str | None = None
    group: str | None = None
    description: str | None = None
    range: list[float | None] | None = None


class AvailableSignalsResponse(BaseModel):
    signals: list[SignalMetadata]
    variants: dict[str, list[str]] = Field(default_factory=dict)


class RuntimeVariantsResponse(BaseModel):
    branch: str
    commit_sha: str
    mode: str = "single"
    variants: list[str]


class ComparisonCreate(BaseModel):
    scenario: str = Field(min_length=1, max_length=1024)
    scenario_source: str = Field(default="bundled", pattern="^(bundled|sftp|managed)$")
    branch: str = Field(min_length=1, max_length=255)
    variants: list[str] = Field(min_length=2, max_length=16)

    @field_validator("variants")
    @classmethod
    def validate_variants(cls, values: list[str]) -> list[str]:
        normalized = [value.strip() for value in values]
        if any(
            not value or not value.replace("-", "").replace("_", "").isalnum()
            for value in normalized
        ):
            raise ValueError("variants contain an invalid execution variant")
        if len(set(normalized)) != len(normalized):
            raise ValueError("duplicate execution variants are not allowed")
        return normalized


class ComparisonRun(BaseModel):
    run_id: RunId
    execution_variant: str
    status: RunStatus
    error_message: str | None = None
    wall_time_sec: float | None = None


class Comparison(BaseModel):
    id: ComparisonId
    status: ComparisonStatus
    scenario_id: ScenarioId
    scenario_source: str
    scenario_name: str
    scenario_type: str | None = None
    scenario_sha256: str
    scenario_path: str
    repository_id: RepositoryId
    branch: str
    commit_sha: CommitSha
    build_id: BuildId
    created_at: datetime
    runs: list[ComparisonRun]
