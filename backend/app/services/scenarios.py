from __future__ import annotations

import hashlib
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any

from app.domain.scenario import InvalidScenarioEntry, ScenarioCatalogEntry
from app.repositories.scenario_catalog import ScenarioCatalogRepository
from app.services.scenario_validator import (
    ScenarioContract,
    ScenarioDocumentError,
    ScenarioValidationUnavailable,
    ScenarioValidator,
)


class InvalidScenario(ValueError):
    pass


@dataclass(frozen=True)
class ScenarioDefinition:
    path: Path
    name: str
    scenario_type: str | None
    legacy_autopilot: str | None
    controller_parameters: tuple[str, ...]
    content: dict[str, Any]
    source: str = "bundled"
    scenario_id: str | None = None
    sha256: str | None = None
    yaml_text: str = ""


class ScenarioService:
    def __init__(
        self,
        scenario_dir: Path,
        validator: ScenarioValidator | None = None,
        *,
        remote_cache_dir: Path | None = None,
        managed_scenario_dir: Path | None = None,
        catalog_repository: ScenarioCatalogRepository | None = None,
    ) -> None:
        self.scenario_dir = scenario_dir.resolve()
        self.validator = validator or ScenarioValidator()
        self.remote_cache_dir = remote_cache_dir.resolve() if remote_cache_dir else None
        self.managed_scenario_dir = (
            managed_scenario_dir.resolve() if managed_scenario_dir else None
        )
        self.catalog_repository = catalog_repository

    def list(self) -> list[str]:
        if not self.scenario_dir.is_dir():
            return []
        return sorted(
            path.relative_to(self.scenario_dir).as_posix()
            for path in self.scenario_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in {".yaml", ".yml"}
        )

    def resolve(self, scenario: str) -> Path:
        normalized = PurePosixPath(scenario)
        if normalized.is_absolute() or ".." in normalized.parts:
            raise InvalidScenario("invalid scenario path")
        candidate = (self.scenario_dir / Path(*normalized.parts)).resolve()
        try:
            candidate.relative_to(self.scenario_dir)
        except ValueError as exc:
            raise InvalidScenario("scenario must be inside JSB_SCENARIO_DIR") from exc
        if candidate.suffix.lower() not in {".yaml", ".yml"} or not candidate.is_file():
            raise InvalidScenario(f"unknown scenario: {scenario}")
        return candidate

    def load(self, scenario: str, source: str = "bundled") -> ScenarioDefinition:
        if source == "bundled":
            path = self.resolve(scenario)
        elif source == "sftp":
            path = self._resolve_remote(scenario)
        elif source == "managed":
            path = self.resolve_managed(scenario)
        else:
            raise InvalidScenario(f"unknown scenario source: {source}")
        try:
            yaml_text = path.read_text(encoding="utf-8")
            document = self.validator.parse_yaml(yaml_text)
        except (OSError, UnicodeError, ScenarioDocumentError) as exc:
            raise InvalidScenario(f"Scenario validation failed: {exc}") from exc
        return ScenarioDefinition(
            path=path,
            name=document.name or scenario,
            scenario_type=document.scenario_type,
            legacy_autopilot=document.autopilot,
            controller_parameters=document.controller_parameters,
            content=document.content,
            source=source,
            scenario_id=scenario,
            sha256=hashlib.sha256(yaml_text.encode("utf-8")).hexdigest(),
            yaml_text=yaml_text,
        )

    def scenario_type_from_snapshot(self, path: Path) -> str | None:
        """Read scenario type from trusted immutable run provenance when DB metadata predates it."""
        try:
            document = self.validator.parse_yaml(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, ScenarioDocumentError):
            return None
        return document.scenario_type

    def catalog(self) -> list[ScenarioCatalogEntry]:
        entries: list[ScenarioCatalogEntry] = []
        for scenario_id in self.list():
            try:
                definition = self.load(scenario_id)
            except InvalidScenario:
                continue
            entries.append(
                ScenarioCatalogEntry(
                    id=scenario_id,
                    name=definition.name,
                    source="bundled",
                    autopilot=definition.legacy_autopilot,
                    scenario_type=definition.scenario_type,
                    schema_version=definition.content.get("schema_version"),
                    controller_parameters=list(definition.controller_parameters),
                    scenario_sha256=definition.sha256 or "",
                )
            )
        for scenario_id in self.list_managed():
            try:
                definition = self.load(scenario_id, "managed")
            except InvalidScenario:
                continue
            entries.append(
                ScenarioCatalogEntry(
                    id=scenario_id,
                    name=definition.name,
                    source="managed",
                    autopilot=definition.legacy_autopilot,
                    scenario_type=definition.scenario_type,
                    schema_version=definition.content.get("schema_version"),
                    controller_parameters=list(definition.controller_parameters),
                    scenario_sha256=definition.sha256 or "",
                )
            )
        if self.catalog_repository is not None:
            for row in self.catalog_repository.list_valid("sftp"):
                try:
                    definition = self.load(row["scenario_id"], "sftp")
                except InvalidScenario:
                    continue
                entries.append(
                    ScenarioCatalogEntry(
                        id=row["scenario_id"],
                        name=row["name"],
                        source="sftp",
                        autopilot=row["autopilot"],
                        scenario_type=definition.scenario_type,
                        schema_version=definition.content.get("schema_version"),
                        controller_parameters=list(definition.controller_parameters),
                        scenario_sha256=row["sha256"],
                        validated_runtime_commit=row["validated_commit"],
                        last_validated_at=row["last_validated_at"],
                    )
                )
        return sorted(entries, key=lambda entry: (entry.name.lower(), entry.source, entry.id))

    def invalid_catalog(self) -> list[InvalidScenarioEntry]:
        entries: list[InvalidScenarioEntry] = []
        for scenario_id in self.list():
            try:
                self.load(scenario_id)
            except InvalidScenario as exc:
                entries.append(
                    InvalidScenarioEntry(
                        id=scenario_id,
                        source="bundled",
                        errors=[
                            {"path": "$", "code": "load", "message": str(exc)}
                        ],
                    )
                )
        for scenario_id in self.list_managed():
            try:
                self.load(scenario_id, "managed")
            except InvalidScenario as exc:
                entries.append(
                    InvalidScenarioEntry(
                        id=scenario_id,
                        source="managed",
                        errors=[
                            {"path": "$", "code": "load", "message": str(exc)}
                        ],
                    )
                )
        if self.catalog_repository is not None:
            entries.extend(
                InvalidScenarioEntry(
                    id=row["scenario_id"],
                    source=row["source"],
                    errors=row["errors"],
                    last_validated_at=row["last_error_at"],
                    validated_runtime_commit=row["last_error_commit"],
                )
                for row in self.catalog_repository.list_invalid()
            )
        return entries

    def list_managed(self) -> list[str]:
        if self.managed_scenario_dir is None or not self.managed_scenario_dir.is_dir():
            return []
        return sorted(
            path.relative_to(self.managed_scenario_dir).as_posix()
            for path in self.managed_scenario_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in {".yaml", ".yml"}
        )

    def resolve_managed(self, scenario: str) -> Path:
        if self.managed_scenario_dir is None:
            raise InvalidScenario("managed scenario source is unavailable")
        normalized = PurePosixPath(scenario)
        if (
            normalized.is_absolute()
            or ".." in normalized.parts
            or "\x00" in scenario
            or "\\" in scenario
        ):
            raise InvalidScenario("invalid scenario path")
        path = self.managed_scenario_dir.joinpath(*normalized.parts).resolve()
        try:
            path.relative_to(self.managed_scenario_dir)
        except ValueError as exc:
            raise InvalidScenario("managed scenario path is unsafe") from exc
        if path.suffix.lower() not in {".yaml", ".yml"} or not path.is_file():
            raise InvalidScenario(f"unknown scenario: {scenario}")
        return path

    def _resolve_remote(self, scenario: str) -> Path:
        if self.remote_cache_dir is None or self.catalog_repository is None:
            raise InvalidScenario("SFTP scenario cache is unavailable")
        normalized = PurePosixPath(scenario)
        if normalized.is_absolute() or ".." in normalized.parts or "\x00" in scenario:
            raise InvalidScenario("invalid scenario path")
        row = self.catalog_repository.get("sftp", scenario)
        if row is None or not row["active"] or not row["valid"] or not row["cache_path"]:
            raise InvalidScenario(f"unknown scenario: {scenario}")
        path = (self.remote_cache_dir / Path(*normalized.parts)).resolve()
        try:
            path.relative_to(self.remote_cache_dir)
        except ValueError as exc:
            raise InvalidScenario("scenario cache path is unsafe") from exc
        if not path.is_file():
            raise InvalidScenario("validated scenario cache is missing")
        return path

    def validate_runtime_contract(
        self, definition: ScenarioDefinition, runtime_worktree: Path
    ) -> None:
        """Validate with the schema shipped by the resolved immutable JSB0 commit."""
        try:
            result = self.validator.validate_against_runtime(
                definition.content, runtime_worktree
            )
        except ScenarioValidationUnavailable as exc:
            raise InvalidScenario(f"Scenario validation failed: {exc}") from exc
        if result.errors:
            first = result.errors[0]
            if first.path in {"autopilot", "autopilot.type"} and first.code == "enum":
                raise InvalidScenario(
                    f"Scenario requires unsupported autopilot '{definition.legacy_autopilot}'."
                )
            prefix = f"{first.path}: " if first.path != "$" else ""
            raise InvalidScenario(f"Scenario validation failed: {prefix}{first.message}")

    @staticmethod
    def load_runtime_contract(runtime_worktree: Path) -> ScenarioContract:
        """Load and validate the scenario contract from a JSB0 checkout."""
        try:
            return ScenarioValidator().load_runtime_contract(runtime_worktree)
        except ScenarioValidationUnavailable as exc:
            raise InvalidScenario(f"Scenario validation failed: {exc}") from exc

    @classmethod
    def runtime_contract_errors(
        cls,
        definition: ScenarioDefinition,
        validator: ScenarioContract,
    ) -> list[str]:
        """Return every schema violation for one loaded scenario definition."""
        result = ScenarioValidator().validate_content(
            definition.content,
            validator,
        )
        messages: list[str] = []
        for error in result.errors:
            if error.path in {"autopilot", "autopilot.type"} and error.code == "enum":
                messages.append(
                    f"Scenario requires unsupported autopilot '{definition.legacy_autopilot}'."
                )
            else:
                prefix = f"{error.path}: " if error.path != "$" else ""
                messages.append(f"Scenario validation failed: {prefix}{error.message}")
        return messages
