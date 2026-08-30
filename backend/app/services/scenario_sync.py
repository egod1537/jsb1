from __future__ import annotations

import hashlib
import threading
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

import paramiko

from app.domain.scenario import ScenarioSyncResult, ScenarioSyncStatus
from app.repositories.scenario_catalog import ScenarioCatalogRepository
from app.services.repository_manager import (
    GitOperationError,
    InvalidRepositoryPath,
    RepositoryManager,
    RuntimeRepositoryNotConfigured,
    RuntimeRepositoryUnavailable,
)
from app.services.scenario_sources.base import ScenarioSource, validate_object_id
from app.services.scenario_validator import (
    ScenarioRuntime,
    ScenarioValidationUnavailable,
    ScenarioValidator,
)
from app.infrastructure.filesystem import AtomicFileStore


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


@dataclass(frozen=True)
class CompatibilityContract:
    runtime_root: Path
    runtime: ScenarioRuntime


class ScenarioCompatibilityResolver:
    """Resolve the latest canonical JSB0 main contract once per operation."""

    def __init__(self, repositories: RepositoryManager) -> None:
        self.repositories = repositories

    def resolve(self) -> CompatibilityContract:
        runtime_repository = self.repositories.runtime_repository()
        self.repositories.fetch(runtime_repository.id)
        revision = self.repositories.resolve_branch(runtime_repository.id, "main")
        worktree = self.repositories.prepare_worktree(
            runtime_repository.id, revision.commit_sha
        )
        return CompatibilityContract(
            runtime_root=worktree,
            runtime=ScenarioRuntime(branch="main", commit=revision.commit_sha),
        )


class ScenarioSyncService:
    def __init__(
        self,
        *,
        source: ScenarioSource | None,
        cache_root: Path,
        catalog: ScenarioCatalogRepository,
        validator: ScenarioValidator,
        compatibility: ScenarioCompatibilityResolver,
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
                error=self.configuration_error or "SFTP scenario source is not configured",
            )
        try:
            compatibility = self.compatibility.resolve()
            contract = self.validator.load_runtime_contract(compatibility.runtime_root)
            objects = self.source.list_objects()
        except Exception as exc:  # noqa: BLE001 - remote/runtime boundary must preserve cache
            message = self._public_error(exc)
            self.catalog.update_sync_status(source="sftp", reachable=False, error=message)
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
                raw = self.source.fetch(item.id)
                yaml_text = raw.decode("utf-8")
                validation = self.validator.validate_yaml(
                    yaml_text,
                    contract,
                    runtime_branch="main",
                    runtime_commit=compatibility.runtime.commit,
                )
            except Exception as exc:  # noqa: BLE001 - one remote object must not abort the batch
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
            relative_cache = validate_object_id(item.id)
            destination = self.cache_root.joinpath(*relative_cache.parts)
            previous = self.catalog.get("sftp", item.id)
            unchanged = (
                previous is not None
                and previous.get("sha256") == digest
                and destination.is_file()
            )
            if not unchanged:
                self.files.write_bytes(destination, raw)
            changed = self.catalog.upsert_valid(
                source="sftp",
                scenario_id=item.id,
                cache_path=destination.relative_to(self.cache_root.parent.parent).as_posix(),
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
            reachable=bool(stored["reachable"]) if stored["reachable"] is not None else None,
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
            return "JSB0 main scenario contract is unavailable"
        if isinstance(exc, paramiko.BadHostKeyException):
            return "SFTP host key verification failed"
        if isinstance(exc, paramiko.AuthenticationException):
            return "SFTP authentication failed"
        if isinstance(exc, paramiko.SSHException):
            detail = str(exc).lower()
            if "known_hosts" in detail or "host key" in detail:
                return "SFTP host key verification failed"
            return "SFTP SSH connection failed"
        if isinstance(exc, UnicodeError):
            return "Remote scenario is not valid UTF-8"
        if isinstance(exc, OSError):
            return "SFTP connection or file read failed"
        return "SFTP scenario sync failed"
