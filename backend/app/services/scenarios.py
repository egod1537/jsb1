from __future__ import annotations

import hashlib
from pathlib import Path
from typing import Any

from app.domain.errors import ContractCompatibilityError
from app.domain.scenario import (
    InvalidScenarioEntry,
    ScenarioCatalogEntry,
    ScenarioDefinition,
    ScenarioSourceContent,
)
from app.domain.scenario_source import ScenarioSource, ScenarioSourceType
from app.domain.scenario_validation import ScenarioRuntime, ScenarioValidationPolicy
from app.infrastructure.filesystem import (
    CatalogCachedScenarioSource,
    DirectoryScenarioSource,
)
from app.repositories.scenario_catalog import ScenarioCatalogRepository
from app.services.scenario_sync import StableScenarioContractResolver
from app.services.scenario_validator import (
    ScenarioContract,
    ScenarioDocumentError,
    ScenarioValidationUnavailable,
    ScenarioValidator,
)


class InvalidScenario(ValueError):
    pass


class ScenarioCatalogUnavailable(ContractCompatibilityError):
    pass


StableValidation = tuple[ScenarioRuntime, ScenarioContract]


class ScenarioService:
    def __init__(
        self,
        scenario_dir: Path,
        validator: ScenarioValidator | None = None,
        *,
        remote_cache_dir: Path | None = None,
        managed_scenario_dir: Path | None = None,
        catalog_repository: ScenarioCatalogRepository | None = None,
        sources: dict[str, ScenarioSource] | None = None,
        stable_contract_resolver: StableScenarioContractResolver | None = None,
    ) -> None:
        self.scenario_dir = scenario_dir.resolve()
        self.validator = validator or ScenarioValidator()
        self.remote_cache_dir = remote_cache_dir.resolve() if remote_cache_dir else None
        self.managed_scenario_dir = (
            managed_scenario_dir.resolve() if managed_scenario_dir else None
        )
        self.catalog_repository = catalog_repository
        self.sources = sources or self._default_sources()
        self.stable_contract_resolver = stable_contract_resolver

    def _default_sources(self) -> dict[str, ScenarioSource]:
        sources: dict[str, ScenarioSource] = {
            "bundled": DirectoryScenarioSource(
                self.scenario_dir, ScenarioSourceType.BUNDLED
            )
        }
        if self.managed_scenario_dir is not None:
            sources["managed"] = DirectoryScenarioSource(
                self.managed_scenario_dir, ScenarioSourceType.MANAGED
            )
        if self.remote_cache_dir is not None and self.catalog_repository is not None:
            sources["sftp"] = CatalogCachedScenarioSource(
                self.remote_cache_dir, self.catalog_repository
            )
        return sources

    def _source(self, source: str) -> ScenarioSource:
        try:
            return self.sources[source]
        except KeyError as exc:
            if source in {"managed", "sftp"}:
                raise InvalidScenario(
                    f"{source} scenario source is unavailable"
                ) from exc
            raise InvalidScenario(f"unknown scenario source: {source}") from exc

    def list_source(self, source: str) -> list[str]:
        try:
            return [item.id for item in self._source(source).list()]
        except (OSError, ValueError) as exc:
            raise InvalidScenario(f"could not list {source} scenarios") from exc

    def list(self) -> list[str]:
        return self.list_source("bundled")

    def resolve(self, scenario: str) -> Path:
        try:
            return self._source_path("bundled", scenario)
        except (OSError, ValueError) as exc:
            raise InvalidScenario(f"unknown scenario: {scenario}") from exc

    def load(self, scenario: str, source: str = "bundled") -> ScenarioDefinition:
        source_content = self.read(scenario, source)
        try:
            document = self.validator.parse_yaml(source_content.yaml_text)
        except ScenarioDocumentError as exc:
            raise InvalidScenario(f"Scenario validation failed: {exc}") from exc
        return ScenarioDefinition(
            scenario_id=scenario,
            source=source,
            source_type=source_content.source_type,
            document=document,
            yaml_text=source_content.yaml_text,
            sha256=source_content.sha256,
            modified_at=source_content.modified_at,
        )

    def read(self, scenario: str, source: str = "bundled") -> ScenarioSourceContent:
        """Read source bytes without interpreting scenario semantics."""
        adapter = self._source(source)
        try:
            raw = adapter.read(scenario)
            yaml_text = raw.decode("utf-8")
        except (OSError, UnicodeError, ValueError) as exc:
            raise InvalidScenario(f"Scenario read failed: {exc}") from exc
        source_object = next(
            (item for item in adapter.list() if item.id == scenario), None
        )
        return ScenarioSourceContent(
            scenario_id=scenario,
            source=source,
            source_type=adapter.source_type,
            yaml_text=yaml_text,
            sha256=hashlib.sha256(raw).hexdigest(),
            modified_at=source_object.modified_at if source_object else None,
        )

    def scenario_type_from_snapshot(self, path: str | Path) -> str | None:
        """Read type from trusted provenance when old DB metadata predates it."""
        try:
            document = self.validator.parse_yaml(
                Path(path).read_text(encoding="utf-8")
            )
        except (OSError, UnicodeError, ScenarioDocumentError):
            return None
        return document.scenario_type

    def catalog(self) -> list[ScenarioCatalogEntry]:
        stable = self._stable_contract()
        entries: list[ScenarioCatalogEntry] = []
        for scenario_id in self.list():
            try:
                definition = self.load(scenario_id)
            except InvalidScenario:
                continue
            if not self._is_catalog_compatible(definition, stable):
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
                    validated_runtime_commit=self._stable_commit(stable),
                )
            )
        for scenario_id in self.list_managed():
            try:
                definition = self.load(scenario_id, "managed")
            except InvalidScenario:
                continue
            if not self._is_catalog_compatible(definition, stable):
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
                    validated_runtime_commit=self._stable_commit(stable),
                )
            )
        if self.catalog_repository is not None:
            for row in self.catalog_repository.list_valid("sftp"):
                try:
                    definition = self.load(row["scenario_id"], "sftp")
                except InvalidScenario:
                    continue
                if not self._is_catalog_compatible(definition, stable):
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
                        validated_runtime_commit=(
                            self._stable_commit(stable) or row["validated_commit"]
                        ),
                        last_validated_at=row["last_validated_at"],
                    )
                )
        return sorted(
            entries,
            key=lambda entry: (entry.name.lower(), entry.source, entry.id),
        )

    def invalid_catalog(self) -> list[InvalidScenarioEntry]:
        stable = self._stable_contract()
        entries: list[InvalidScenarioEntry] = []
        for scenario_id in self.list():
            try:
                definition = self.load(scenario_id)
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
                continue
            validation = self._catalog_validation(definition, stable)
            if validation is not None and not validation.valid:
                entries.append(
                    InvalidScenarioEntry(
                        id=scenario_id,
                        source="bundled",
                        errors=[error.model_dump() for error in validation.errors],
                        last_validated_at=None,
                        validated_runtime_commit=(
                            validation.runtime.commit if validation.runtime else None
                        ),
                    )
                )
        for scenario_id in self.list_managed():
            try:
                definition = self.load(scenario_id, "managed")
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
                continue
            validation = self._catalog_validation(definition, stable)
            if validation is not None and not validation.valid:
                entries.append(
                    InvalidScenarioEntry(
                        id=scenario_id,
                        source="managed",
                        errors=[error.model_dump() for error in validation.errors],
                        last_validated_at=None,
                        validated_runtime_commit=(
                            validation.runtime.commit if validation.runtime else None
                        ),
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
            known = {(entry.source, entry.id) for entry in entries}
            for row in self.catalog_repository.list_valid("sftp"):
                key = ("sftp", str(row["scenario_id"]))
                if key in known:
                    continue
                try:
                    definition = self.load(key[1], "sftp")
                except InvalidScenario as exc:
                    entries.append(
                        InvalidScenarioEntry(
                            id=key[1],
                            source="sftp",
                            errors=[
                                {"path": "$", "code": "load", "message": str(exc)}
                            ],
                        )
                    )
                    continue
                validation = self._catalog_validation(definition, stable)
                if validation is not None and not validation.valid:
                    entries.append(
                        InvalidScenarioEntry(
                            id=key[1],
                            source="sftp",
                            errors=[
                                error.model_dump() for error in validation.errors
                            ],
                            validated_runtime_commit=(
                                validation.runtime.commit
                                if validation.runtime
                                else None
                            ),
                        )
                    )
        return entries

    def _stable_contract(self) -> StableValidation | None:
        if self.stable_contract_resolver is None:
            return None
        try:
            compatibility = self.stable_contract_resolver.resolve()
            contract = self.validator.load_runtime_contract(compatibility.runtime_root)
        except Exception as exc:  # noqa: BLE001 - use-case compatibility error
            raise ScenarioCatalogUnavailable(
                "Configured JSB0 stable scenario contract is unavailable"
            ) from exc
        return compatibility.runtime, contract

    def _catalog_validation(
        self,
        definition: ScenarioDefinition,
        stable: StableValidation | None,
    ):
        if stable is None:
            return None
        runtime, contract = stable
        return self.validator.evaluate_document(
            definition.document,
            contract,
            runtime_branch=runtime.branch,
            runtime_commit=runtime.commit,
            policy=ScenarioValidationPolicy.CATALOG_STABLE,
        ).result

    def _is_catalog_compatible(
        self,
        definition: ScenarioDefinition,
        stable: StableValidation | None,
    ) -> bool:
        validation = self._catalog_validation(definition, stable)
        return validation is None or validation.valid

    @staticmethod
    def _stable_commit(stable: StableValidation | None) -> str | None:
        return stable[0].commit if stable is not None else None

    def list_managed(self) -> list[str]:
        if "managed" not in self.sources:
            return []
        return self.list_source("managed")

    def resolve_managed(self, scenario: str) -> Path:
        try:
            return self._source_path("managed", scenario)
        except (OSError, ValueError) as exc:
            raise InvalidScenario(f"unknown scenario: {scenario}") from exc

    def _resolve_remote(self, scenario: str) -> Path:
        try:
            return self._source_path("sftp", scenario)
        except (OSError, ValueError) as exc:
            raise InvalidScenario(f"unknown scenario: {scenario}") from exc

    def _source_path(self, source: str, scenario: str) -> Path:
        adapter = self._source(source)
        path_method = getattr(adapter, "path", None)
        if path_method is None:
            raise InvalidScenario(f"{source} source does not expose a local path")
        return path_method(scenario)

    def validate_runtime_contract(
        self,
        definition: ScenarioDefinition,
        runtime_worktree: Path,
        *,
        reader: Any | None = None,
    ) -> None:
        """Validate with the schema shipped by the resolved immutable JSB0 commit."""
        try:
            document = definition.document
            result = self.validator.evaluate_document(
                document,
                self.validator.load_runtime_contract(
                    runtime_worktree, reader=reader
                ),
                policy=ScenarioValidationPolicy.RUN_EXACT,
            ).result
        except (ScenarioDocumentError, ScenarioValidationUnavailable) as exc:
            raise InvalidScenario(f"Scenario validation failed: {exc}") from exc
        if result.errors:
            first = result.errors[0]
            if first.path in {"autopilot", "autopilot.type"} and first.code == "enum":
                raise InvalidScenario(
                    "Scenario requires unsupported autopilot "
                    f"'{definition.legacy_autopilot}'."
                )
            prefix = f"{first.path}: " if first.path != "$" else ""
            raise InvalidScenario(
                f"Scenario validation failed: {prefix}{first.message}"
            )

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
                    "Scenario requires unsupported autopilot "
                    f"'{definition.legacy_autopilot}'."
                )
            else:
                prefix = f"{error.path}: " if error.path != "$" else ""
                messages.append(f"Scenario validation failed: {prefix}{error.message}")
        return messages
