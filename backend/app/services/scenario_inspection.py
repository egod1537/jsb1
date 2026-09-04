from __future__ import annotations

import hashlib
import json
from datetime import UTC, datetime
from typing import Any

from app.domain.scenario import (
    ScenarioInspectionCatalogEntry,
    ScenarioInspectionDetail,
    ScenarioInspectionValidation,
    ScenarioProvenance,
)
from app.domain.scenario_validation import ScenarioValidationPolicy
from app.repositories.scenario_catalog import ScenarioCatalogRepository
from app.services.scenario_validator import (
    ScenarioContract,
    ScenarioDocumentError,
    ScenarioRuntime,
    ScenarioValidationError,
    ScenarioValidator,
)
from app.services.scenarios import InvalidScenario, ScenarioService


class ScenarioInspectionService:
    """Read-only catalog/detail projection over existing scenario sources."""

    def __init__(
        self,
        scenarios: ScenarioService,
        validator: ScenarioValidator,
        catalog: ScenarioCatalogRepository,
    ) -> None:
        self.scenarios = scenarios
        self.validator = validator
        self.catalog_repository = catalog

    def catalog(
        self, contract: ScenarioContract, runtime: ScenarioRuntime
    ) -> list[ScenarioInspectionCatalogEntry]:
        entries = [
            self._inspect_source(
                source="bundled",
                scenario_id=scenario_id,
                contract=contract,
                runtime=runtime,
            )
            for scenario_id in self.scenarios.list()
        ]
        entries.extend(
            self._inspect_source(
                source="managed",
                scenario_id=scenario_id,
                contract=contract,
                runtime=runtime,
            )
            for scenario_id in self.scenarios.list_managed()
        )
        for row in self.catalog_repository.list_active("sftp"):
            scenario_id = str(row["scenario_id"])
            if row.get("valid") and row.get("cache_path"):
                try:
                    source_content = self.scenarios.read(scenario_id, "sftp")
                    entries.append(
                        self._inspect_text(
                            source="sftp",
                            scenario_id=scenario_id,
                            yaml_text=source_content.yaml_text,
                            contract=contract,
                            runtime=runtime,
                            updated_at=row.get("last_sync_at"),
                        )
                    )
                    continue
                except InvalidScenario:
                    pass
            entries.append(self._invalid_remote_entry(row, runtime))
        return sorted(
            entries,
            key=lambda item: (item.name.lower(), item.source, item.path),
        )

    def detail(
        self,
        source: str,
        scenario_id: str,
        contract: ScenarioContract,
        runtime: ScenarioRuntime,
    ) -> ScenarioInspectionDetail:
        if source == "bundled":
            if scenario_id not in self.scenarios.list():
                raise InvalidScenario(f"unknown scenario: {scenario_id}")
            return self._inspect_source(
                source=source,
                scenario_id=scenario_id,
                contract=contract,
                runtime=runtime,
                include_content=True,
            )
        if source == "managed":
            if scenario_id not in self.scenarios.list_managed():
                raise InvalidScenario(f"unknown scenario: {scenario_id}")
            return self._inspect_source(
                source=source,
                scenario_id=scenario_id,
                contract=contract,
                runtime=runtime,
                include_content=True,
            )
        if source != "sftp":
            raise InvalidScenario("unknown scenario source")
        row = self.catalog_repository.get("sftp", scenario_id)
        if row is None or not row.get("active"):
            raise InvalidScenario(f"unknown scenario: {scenario_id}")
        if row.get("valid") and row.get("cache_path"):
            source_content = self.scenarios.read(scenario_id, "sftp")
            return self._inspect_text(
                source="sftp",
                scenario_id=scenario_id,
                yaml_text=source_content.yaml_text,
                contract=contract,
                runtime=runtime,
                updated_at=row.get("last_sync_at"),
                include_content=True,
            )
        invalid = self._invalid_remote_entry(row, runtime)
        return ScenarioInspectionDetail(
            **invalid.model_dump(),
            definition=None,
            raw_yaml=None,
            provenance=ScenarioProvenance(
                authority="SFTP validation metadata",
                expected_sha256=row.get("sha256"),
                actual_sha256=None,
                integrity="unknown",
            ),
        )

    def snapshot_detail(
        self,
        *,
        scenario_id: str,
        name: str,
        relative_path: str,
        yaml_text: str,
        expected_sha256: str | None,
        authority: str,
        contract: ScenarioContract | None,
        runtime_branch: str | None,
        runtime_commit: str | None,
    ) -> ScenarioInspectionDetail:
        actual_sha = hashlib.sha256(yaml_text.encode("utf-8")).hexdigest()
        if contract is None:
            validation = ScenarioInspectionValidation(
                valid=None,
                runtime_branch=runtime_branch,
                runtime_commit=runtime_commit,
                errors=[
                    {
                        "path": "$",
                        "code": "runtime_unavailable",
                        "message": "Historical JSB0 scenario contract is unavailable",
                    }
                ],
            )
        else:
            evaluation = self.validator.evaluate_yaml(
                yaml_text,
                contract,
                runtime_branch=runtime_branch,
                runtime_commit=runtime_commit,
                policy=ScenarioValidationPolicy.RUN_EXACT,
            )
            result = evaluation.result
            validation = self._validation(
                result.valid,
                result.errors,
                result.runtime,
            )
        if contract is None:
            document, content = self._parse_best_effort(yaml_text)
        else:
            document = evaluation.document
            content = document.content if document else None
        return ScenarioInspectionDetail(
            id=f"run_snapshot:{scenario_id}",
            source="run_snapshot",
            path=relative_path,
            name=document.name if document and document.name else name,
            scenario_type=document.scenario_type if document else None,
            schema_version=document.schema_version if document else None,
            controller_parameters=(
                list(document.controller_parameters) if document else []
            ),
            sha256=actual_sha,
            validation=validation,
            definition=content,
            raw_yaml=yaml_text,
            provenance=ScenarioProvenance(
                authority=authority,
                expected_sha256=expected_sha256,
                actual_sha256=actual_sha,
                integrity=(
                    "unknown" if expected_sha256 is None
                    else "verified" if expected_sha256 == actual_sha
                    else "mismatch"
                ),
            ),
        )

    def _inspect_source(
        self,
        *,
        source: str,
        scenario_id: str,
        contract: ScenarioContract,
        runtime: ScenarioRuntime,
        include_content: bool = False,
    ) -> ScenarioInspectionCatalogEntry | ScenarioInspectionDetail:
        try:
            source_content = self.scenarios.read(scenario_id, source)
            updated_at = (
                datetime.fromtimestamp(source_content.modified_at, tz=UTC).isoformat()
                if source_content.modified_at is not None
                else None
            )
        except InvalidScenario as exc:
            unreadable = self._unreadable_entry(source, scenario_id, str(exc), runtime)
            if not include_content:
                return unreadable
            return ScenarioInspectionDetail(
                **unreadable.model_dump(),
                definition=None,
                raw_yaml=None,
                provenance=ScenarioProvenance(
                    authority="unreadable source",
                    integrity="unknown",
                ),
            )
        return self._inspect_text(
            source=source,
            scenario_id=scenario_id,
            yaml_text=source_content.yaml_text,
            contract=contract,
            runtime=runtime,
            updated_at=updated_at,
            include_content=include_content,
        )

    def _inspect_text(
        self,
        *,
        source: str,
        scenario_id: str,
        yaml_text: str,
        contract: ScenarioContract,
        runtime: ScenarioRuntime,
        updated_at: str | None,
        include_content: bool = False,
    ) -> ScenarioInspectionCatalogEntry | ScenarioInspectionDetail:
        evaluation = self.validator.evaluate_yaml(
            yaml_text,
            contract,
            runtime_branch=runtime.branch,
            runtime_commit=runtime.commit,
            policy=ScenarioValidationPolicy.CATALOG_STABLE,
        )
        result = evaluation.result
        document = evaluation.document
        content = document.content if document else None
        digest = hashlib.sha256(yaml_text.encode("utf-8")).hexdigest()
        values: dict[str, Any] = {
            "id": f"{source}:{scenario_id}",
            "source": source,
            "path": scenario_id,
            "name": document.name if document and document.name else scenario_id,
            "scenario_type": document.scenario_type if document else None,
            "schema_version": document.schema_version if document else None,
            "controller_parameters": (
                list(document.controller_parameters) if document else []
            ),
            "sha256": digest,
            "validation": self._validation(
                result.valid,
                result.errors,
                result.runtime,
            ),
            "updated_at": updated_at,
        }
        if not include_content:
            return ScenarioInspectionCatalogEntry(**values)
        return ScenarioInspectionDetail(
            **values,
            definition=content,
            raw_yaml=yaml_text,
            provenance=ScenarioProvenance(
                authority=(
                    "repository file"
                    if source == "bundled"
                    else "managed scenario"
                    if source == "managed"
                    else "validated SFTP cache"
                ),
                expected_sha256=digest,
                actual_sha256=digest,
                integrity="verified",
            ),
        )

    def _invalid_remote_entry(
        self, row: dict[str, Any], runtime: ScenarioRuntime
    ) -> ScenarioInspectionCatalogEntry:
        try:
            errors = json.loads(row.get("last_error") or "[]")
        except json.JSONDecodeError:
            errors = [
                {
                    "path": "$",
                    "code": "sync",
                    "message": str(row.get("last_error")),
                }
            ]
        return ScenarioInspectionCatalogEntry(
            id=f"sftp:{row['scenario_id']}",
            source="sftp",
            path=row["scenario_id"],
            name=row.get("name") or row["scenario_id"],
            scenario_type=None,
            schema_version=None,
            sha256=row.get("sha256"),
            validation=ScenarioInspectionValidation(
                valid=False,
                runtime_branch=runtime.branch,
                runtime_commit=row.get("last_error_commit") or runtime.commit,
                errors=errors,
            ),
            updated_at=row.get("last_error_at") or row.get("last_sync_at"),
        )

    @staticmethod
    def _validation(
        valid: bool,
        errors: list[ScenarioValidationError],
        runtime: ScenarioRuntime | None,
    ) -> ScenarioInspectionValidation:
        return ScenarioInspectionValidation(
            valid=valid,
            runtime_branch=runtime.branch if runtime else None,
            runtime_commit=runtime.commit if runtime else None,
            errors=[error.model_dump() for error in errors],
        )

    def _parse_best_effort(self, yaml_text: str):
        try:
            document = self.validator.parse_yaml(yaml_text)
            return document, document.content
        except ScenarioDocumentError:
            return None, None

    @staticmethod
    def _unreadable_entry(
        source: str,
        scenario_id: str,
        message: str,
        runtime: ScenarioRuntime,
    ) -> ScenarioInspectionCatalogEntry:
        return ScenarioInspectionCatalogEntry(
            id=f"{source}:{scenario_id}",
            source=source,
            path=scenario_id,
            name=scenario_id,
            validation=ScenarioInspectionValidation(
                valid=False,
                runtime_branch=runtime.branch,
                runtime_commit=runtime.commit,
                errors=[{"path": "$", "code": "read_error", "message": message}],
            ),
        )
