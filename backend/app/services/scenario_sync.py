from __future__ import annotations

import hashlib
import threading
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path

from app.domain.scenario import ScenarioSyncResult, ScenarioSyncStatus
from app.domain.scenario_source import ScenarioSource, validate_object_id
from app.domain.scenario_validation import ScenarioValidationPolicy
from app.infrastructure.filesystem import AtomicFileStore
from app.infrastructure.scenario import public_sftp_error
from app.repositories.scenario_catalog import ScenarioCatalogRepository
from app.services.repository_manager import (
    GitOperationError,
    InvalidRepositoryPath,
    RepositoryManager,
    RuntimeRepositoryNotConfigured,
    RuntimeRepositoryUnavailable,
)
from app.services.scenario_validator import (
    ScenarioRuntime,
    ScenarioValidationUnavailable,
    ScenarioValidator,
)


def utc_now() -> str:
    return datetime.now(UTC).isoformat()


@dataclass(frozen=True)
class StableScenarioContract:
    runtime_root: Path
    runtime: ScenarioRuntime


class StableScenarioContractResolver:
    """Resolve the stable/default JSB0 contract used only for catalog workflows."""

    def __init__(self, repositories: RepositoryManager) -> None:
        self.repositories = repositories

    def resolve(self) -> StableScenarioContract:
        runtime_repository = self.repositories.runtime_repository()
        self.repositories.fetch(runtime_repository.id)
        branch = runtime_repository.default_branch
        revision = self.repositories.resolve_branch(runtime_repository.id, branch)
        worktree = self.repositories.prepare_worktree(
            runtime_repository.id, revision.commit_sha
        )
        return StableScenarioContract(
            runtime_root=worktree,
            runtime=ScenarioRuntime(branch=branch, commit=revision.commit_sha),
        )


# Compatibility names for extensions importing the pre-policy terminology.
CompatibilityContract = StableScenarioContract
ScenarioCompatibilityResolver = StableScenarioContractResolver


class ScenarioSyncService:
    def __init__(
        self,
        *,
        source: ScenarioSource | None,
        cache_root: Path,
        catalog: ScenarioCatalogRepository,
        validator: ScenarioValidator,
        compatibility: StableScenarioContractResolver,
        configuration_error: str | None = None,
        files: AtomicFileStore | None = None,
    ) -> None:
        self.source = source
        self.cache_root = cache_root.resolve()
        self.catalog = catalog
        self.validator = validator
        self.compatibility = compatibility
        self.configuration_error = configuration_error
        self.files = files or AtomicFileStore()
        self._sync_lock = threading.Lock()

    @property
    def configured(self) -> bool:
        return self.source is not None or self.configuration_error is not None

    def sync(self) -> ScenarioSyncResult:
        with self._sync_lock:
            return self._sync()

    def _sync(self) -> ScenarioSyncResult:
        if self.source is None:
            return ScenarioSyncResult(
                configured=self.configured,
                reachable=False,
                error=(
                    self.configuration_error
                    or "SFTP scenario source is not configured"
                ),
            )
        try:
            compatibility = self.compatibility.resolve()
            contract = self.validator.load_runtime_contract(compatibility.runtime_root)
            objects = self.source.list()
        except Exception as exc:  # noqa: BLE001 - expose a stable sync failure
            message = self._public_error(exc)
            self.catalog.update_sync_status(
                source="sftp", reachable=False, error=message
            )
            return ScenarioSyncResult(configured=True, reachable=False, error=message)

        now = utc_now()
        result = ScenarioSyncResult(
            configured=True,
            reachable=True,
            fetched=len(objects),
            runtime_commit=compatibility.runtime.commit,
        )
        present: list[str] = []
        for item in objects:
            present.append(item.id)
            try:
                raw = self.source.read(item.id)
                yaml_text = raw.decode("utf-8")
                evaluation = self.validator.evaluate_yaml(
                    yaml_text,
                    contract,
                    runtime_branch=compatibility.runtime.branch,
                    runtime_commit=compatibility.runtime.commit,
                    policy=ScenarioValidationPolicy.CATALOG_STABLE,
                )
                validation = evaluation.result
            except Exception as exc:  # noqa: BLE001 - isolate invalid remote objects
                validation = None
                errors = [
                    {
                        "path": "$",
                        "code": "fetch",
                        "message": self._public_error(exc),
                    }
                ]
            else:
                errors = [error.model_dump() for error in validation.errors]
            if (
                validation is None
                or not validation.valid
                or validation.scenario is None
                or validation.scenario.name is None
            ):
                result.invalid += 1
                if not errors:
                    errors = [
                        {
                            "path": "$",
                            "code": "metadata",
                            "message": "Scenario name is required",
                        }
                    ]
                self.catalog.record_invalid(
                    source="sftp",
                    scenario_id=item.id,
                    errors=errors,
                    validated_commit=compatibility.runtime.commit,
                    synced_at=now,
                )
                continue
            digest = hashlib.sha256(raw).hexdigest()
            validate_object_id(item.id)
            destination = self.cache_root / "objects" / digest[:2] / f"{digest}.yaml"
            previous = self.catalog.get("sftp", item.id)
            unchanged = (
                previous is not None
                and previous.get("sha256") == digest
                and destination.is_file()
            )
            if not unchanged:
                # Content-addressing means publishing new bytes can never replace
                # the last-known-good object referenced by the catalog.
                self.files.write_bytes(destination, raw)
            changed = self.catalog.upsert_valid(
                source="sftp",
                scenario_id=item.id,
                cache_path=destination.relative_to(self.cache_root).as_posix(),
                name=validation.scenario.name,
                autopilot=validation.scenario.autopilot,
                sha256=digest,
                validated_commit=compatibility.runtime.commit,
                synced_at=now,
            )
            result.valid += 1
            if changed or not unchanged:
                result.updated += 1
            else:
                result.unchanged += 1
        result.removed = self.catalog.mark_missing("sftp", present, now)
        self.catalog.update_sync_status(source="sftp", reachable=True, error=None)
        return result

    def status(self) -> ScenarioSyncStatus:
        stored = self.catalog.sync_status("sftp")
        if stored is None:
            return ScenarioSyncStatus(configured=self.configured)
        return ScenarioSyncStatus(
            configured=self.configured,
            reachable=(
                bool(stored["reachable"])
                if stored["reachable"] is not None
                else None
            ),
            last_sync_at=stored["last_sync_at"],
            last_success_at=stored["last_success_at"],
            last_error=stored["last_error"],
        )

    @staticmethod
    def _public_error(exc: Exception) -> str:
        if isinstance(exc, ScenarioValidationUnavailable):
            return str(exc)
        if isinstance(
            exc,
            (
                GitOperationError,
                InvalidRepositoryPath,
                RuntimeRepositoryNotConfigured,
                RuntimeRepositoryUnavailable,
            ),
        ):
            return "configured JSB0 scenario contract is unavailable"
        sftp_message = public_sftp_error(exc)
        if sftp_message is not None:
            return sftp_message
        if isinstance(exc, UnicodeError):
            return "Remote scenario is not valid UTF-8"
        if isinstance(exc, OSError):
            return "SFTP connection or file read failed"
        return "SFTP scenario sync failed"
