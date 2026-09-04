from __future__ import annotations

import hashlib
from pathlib import Path

from app.domain.scenario import ScenarioCreateResponse
from app.domain.scenario_source import (
    InvalidScenarioObjectId,
    validate_object_id,
)
from app.domain.scenario_validation import (
    ScenarioValidationPolicy,
    ScenarioValidationResult,
)
from app.infrastructure.filesystem import (
    ManagedScenarioStore,
    UnsafeManagedScenarioPath,
)
from app.services.scenario_sync import StableScenarioContractResolver
from app.services.scenario_validator import ScenarioValidator


class ManagedScenarioPathError(ValueError):
    pass


class ManagedScenarioConflict(ValueError):
    pass


class ManagedScenarioValidationFailed(ValueError):
    def __init__(self, validation: ScenarioValidationResult) -> None:
        self.validation = validation
        super().__init__("scenario is not compatible with the canonical JSB0 contract")


class ScenarioWriteService:
    """Validate and publish operator-created scenarios into a managed local source."""

    def __init__(
        self,
        root: Path,
        validator: ScenarioValidator,
        compatibility: StableScenarioContractResolver,
        store: ManagedScenarioStore | None = None,
    ) -> None:
        self.root = root.resolve()
        self.validator = validator
        self.compatibility = compatibility
        self.store = store or ManagedScenarioStore(root)

    def create(self, scenario_path: str, yaml_text: str) -> ScenarioCreateResponse:
        try:
            normalized = validate_object_id(scenario_path)
        except InvalidScenarioObjectId as exc:
            raise ManagedScenarioPathError(str(exc)) from exc

        compatibility = self.compatibility.resolve()
        contract = self.validator.load_runtime_contract(compatibility.runtime_root)
        validation = self.validator.evaluate_yaml(
            yaml_text,
            contract,
            runtime_branch=compatibility.runtime.branch,
            runtime_commit=compatibility.runtime.commit,
            policy=ScenarioValidationPolicy.CATALOG_STABLE,
        ).result
        if not validation.valid:
            raise ManagedScenarioValidationFailed(validation)

        try:
            self.store.create(normalized, yaml_text)
        except FileExistsError as exc:
            raise ManagedScenarioConflict(
                f"scenario already exists: {normalized.as_posix()}"
            ) from exc
        except UnsafeManagedScenarioPath as exc:
            raise ManagedScenarioPathError(str(exc)) from exc

        digest = hashlib.sha256(yaml_text.encode("utf-8")).hexdigest()
        return ScenarioCreateResponse(
            id=normalized.as_posix(),
            path=normalized.as_posix(),
            scenario_sha256=digest,
            validation=validation,
        )
