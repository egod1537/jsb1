from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from pydantic import BaseModel, Field

from app.domain.scenario_source import ScenarioSourceType
from app.domain.scenario_validation import ScenarioDocument, ScenarioValidationResult


@dataclass(frozen=True)
class ScenarioSourceContent:
    """Uninterpreted text and provenance returned by a source adapter."""

    scenario_id: str
    source: str
    source_type: ScenarioSourceType
    yaml_text: str
    sha256: str
    modified_at: float | None = None


@dataclass(frozen=True)
class ScenarioDefinition:
    """Source-independent parsed scenario used by every application workflow."""

    scenario_id: str
    source: str
    source_type: ScenarioSourceType
    document: ScenarioDocument
    yaml_text: str
    sha256: str
    modified_at: float | None = None

    @property
    def name(self) -> str:
        return self.document.name or self.scenario_id

    @property
    def scenario_type(self) -> str | None:
        return self.document.scenario_type

    @property
    def legacy_autopilot(self) -> str | None:
        return self.document.autopilot

    @property
    def controller_parameters(self) -> tuple[str, ...]:
        return self.document.controller_parameters

    @property
    def content(self) -> dict[str, Any]:
        return self.document.content


class ScenarioCatalogEntry(BaseModel):
    id: str
    name: str
    source: str
    autopilot: str | None = None
    valid: bool = True
    scenario_type: str | None = None
    schema_version: int | None = None
    controller_parameters: list[str] = Field(default_factory=list)
    scenario_sha256: str
    validated_runtime_commit: str | None = None
    last_validated_at: str | None = None


class InvalidScenarioEntry(BaseModel):
    id: str
    source: str
    errors: list[dict[str, str]]
    last_validated_at: str | None = None
    validated_runtime_commit: str | None = None


class ScenarioValidateRequest(BaseModel):
    yaml: str = Field(min_length=1, max_length=1_000_000)


class ScenarioCreateRequest(BaseModel):
    path: str = Field(min_length=1, max_length=1024)
    yaml: str = Field(min_length=1, max_length=1_000_000)


class ScenarioCreateResponse(BaseModel):
    id: str
    source: str = "managed"
    path: str
    scenario_sha256: str
    validation: ScenarioValidationResult


class ScenarioBatchItem(BaseModel):
    id: str = Field(min_length=1, max_length=1024)
    yaml: str = Field(min_length=1, max_length=1_000_000)


class ScenarioBatchRequest(BaseModel):
    scenarios: list[ScenarioBatchItem] = Field(min_length=1, max_length=100)


class ScenarioBatchResult(BaseModel):
    id: str
    validation: ScenarioValidationResult


class ScenarioBatchResponse(BaseModel):
    runtime_commit: str
    total: int
    valid: int
    invalid: int
    results: list[ScenarioBatchResult]


class ScenarioSyncResult(BaseModel):
    source: str = "sftp"
    configured: bool
    reachable: bool
    fetched: int = 0
    valid: int = 0
    invalid: int = 0
    updated: int = 0
    unchanged: int = 0
    removed: int = 0
    runtime_commit: str | None = None
    error: str | None = None


class ScenarioSyncStatus(BaseModel):
    configured: bool
    reachable: bool | None = None
    last_sync_at: str | None = None
    last_success_at: str | None = None
    last_error: str | None = None


class ScenarioInspectionValidation(BaseModel):
    valid: bool | None
    runtime_branch: str | None = None
    runtime_commit: str | None = None
    errors: list[dict[str, str]] = Field(default_factory=list)


class ScenarioProvenance(BaseModel):
    authority: str
    expected_sha256: str | None = None
    actual_sha256: str | None = None
    integrity: str = Field(pattern="^(verified|mismatch|unknown)$")


class ScenarioInspectionCatalogEntry(BaseModel):
    id: str
    source: str
    path: str
    name: str
    scenario_type: str | None = None
    schema_version: int | None = None
    controller_parameters: list[str] = Field(default_factory=list)
    sha256: str | None = None
    validation: ScenarioInspectionValidation
    updated_at: str | None = None


class ScenarioInspectionDetail(ScenarioInspectionCatalogEntry):
    definition: dict[str, Any] | None = None
    raw_yaml: str | None = None
    provenance: ScenarioProvenance
