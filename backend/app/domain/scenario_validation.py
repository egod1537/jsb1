from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from typing import Any

from pydantic import BaseModel


class ScenarioValidationError(BaseModel):
    path: str
    code: str
    message: str


class ScenarioSummary(BaseModel):
    name: str | None
    autopilot: str | None


class ScenarioRuntime(BaseModel):
    branch: str
    commit: str


class ScenarioValidationPolicy(StrEnum):
    """Why a contract is being applied to scenario bytes."""

    CATALOG_STABLE = "catalog_stable"
    RUN_EXACT = "run_exact"


class ScenarioValidationResult(BaseModel):
    valid: bool
    scenario: ScenarioSummary | None = None
    runtime: ScenarioRuntime | None = None
    schema_version: int | None = None
    errors: list[ScenarioValidationError]


@dataclass(frozen=True)
class ScenarioDocument:
    content: dict[str, Any]
    name: str | None
    scenario_type: str | None
    autopilot: str | None
    schema_version: int | None
    controller_parameters: tuple[str, ...]


@dataclass(frozen=True)
class ScenarioEvaluation:
    """One parse shared by validation, metadata, inspection, and snapshots."""

    document: ScenarioDocument | None
    result: ScenarioValidationResult
    policy: ScenarioValidationPolicy


class ScenarioDocumentError(ValueError):
    def __init__(self, errors: list[ScenarioValidationError]) -> None:
        self.errors = errors
        super().__init__(errors[0].message if errors else "Scenario validation failed")
