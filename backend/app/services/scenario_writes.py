from __future__ import annotations

import hashlib
import os
import tempfile
from pathlib import Path

from app.domain.scenario import ScenarioCreateResponse
from app.domain.scenario_validation import ScenarioValidationResult
from app.services.scenario_sources.base import (
    InvalidScenarioObjectId,
    validate_object_id,
)
from app.services.scenario_sync import ScenarioCompatibilityResolver
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
        compatibility: ScenarioCompatibilityResolver,
    ) -> None:
        self.root = root.resolve()
        self.validator = validator
        self.compatibility = compatibility

    def create(self, scenario_path: str, yaml_text: str) -> ScenarioCreateResponse:
        try:
            normalized = validate_object_id(scenario_path)
        except InvalidScenarioObjectId as exc:
            raise ManagedScenarioPathError(str(exc)) from exc

        compatibility = self.compatibility.resolve()
        contract = self.validator.load_runtime_contract(compatibility.runtime_root)
        validation = self.validator.validate_yaml(
            yaml_text,
            contract,
            runtime_branch=compatibility.runtime.branch,
            runtime_commit=compatibility.runtime.commit,
        )
        if not validation.valid:
            raise ManagedScenarioValidationFailed(validation)

        self.root.mkdir(parents=True, exist_ok=True)
        parent = self.root
        for part in normalized.parts[:-1]:
            child = parent / part
            if child.is_symlink():
                raise ManagedScenarioPathError(
                    "scenario path contains a symbolic link"
                )
            child.mkdir(exist_ok=True)
            try:
                child.resolve().relative_to(self.root)
            except ValueError as exc:
                raise ManagedScenarioPathError(
                    "scenario path escapes the managed root"
                ) from exc
            parent = child
        target = parent / normalized.name
        if target.exists():
            raise ManagedScenarioConflict(f"scenario already exists: {normalized.as_posix()}")

        temporary_path: Path | None = None
        try:
            descriptor, temporary_name = tempfile.mkstemp(
                dir=target.parent,
                prefix=f".{target.name}.",
                suffix=".tmp",
            )
            temporary_path = Path(temporary_name)
            with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
                stream.write(yaml_text)
                stream.flush()
                os.fsync(stream.fileno())
            try:
                os.link(temporary_path, target)
            except FileExistsError as exc:
                raise ManagedScenarioConflict(
                    f"scenario already exists: {normalized.as_posix()}"
                ) from exc
        finally:
            if temporary_path is not None:
                temporary_path.unlink(missing_ok=True)

        digest = hashlib.sha256(yaml_text.encode("utf-8")).hexdigest()
        return ScenarioCreateResponse(
            id=normalized.as_posix(),
            path=normalized.as_posix(),
            scenario_sha256=digest,
            validation=validation,
        )
