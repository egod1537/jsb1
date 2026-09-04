from __future__ import annotations

import logging
from pathlib import Path

from app.domain.models import RunStatus
from app.infrastructure.filesystem import RunDirectoryStore, UnsafeDirectory
from app.repositories.runs import RunRepository

logger = logging.getLogger(__name__)


class ActiveRunDeletionNotAllowed(RuntimeError):
    pass


class UnsafeRunDirectory(RuntimeError):
    pass


class RunDeletionService:
    """Deletes terminal Run-owned state without touching shared runtime/build data."""

    def __init__(
        self,
        runs: RunRepository,
        runs_dir: Path,
        directories: RunDirectoryStore | None = None,
    ) -> None:
        self.runs = runs
        self.directories = directories or RunDirectoryStore(runs_dir)

    def delete(self, run_id: int) -> None:
        run = self.runs.get(run_id)
        if run.status in {RunStatus.QUEUED, RunStatus.RUNNING}:
            raise ActiveRunDeletionNotAllowed(
                f"run {run_id} is {run.status.value} and cannot be deleted"
            )

        try:
            staged = self.directories.stage(run_id)
        except UnsafeDirectory as exc:
            raise UnsafeRunDirectory(
                f"run {run_id} artifact path is not a safe directory"
            ) from exc

        try:
            result = self.runs.delete_terminal(run_id)
        except Exception:
            self.directories.rollback(staged)
            raise
        if not result.deleted:
            self.directories.rollback(staged)
            current = self.runs.get(run_id)
            raise ActiveRunDeletionNotAllowed(
                f"run {run_id} is {current.status.value} and cannot be deleted"
            )
        try:
            self.directories.commit(staged)
        except OSError:
            # DB deletion is authoritative. The inaccessible staged directory is
            # an orphan eligible for maintenance cleanup, never live Run data.
            logger.exception("could not purge staged run directory id=%s", run_id)
        if result.orphaned_comparison_id is not None:
            try:
                self.directories.delete_comparison(result.orphaned_comparison_id)
            except (OSError, UnsafeDirectory):
                logger.exception(
                    "could not purge orphaned comparison directory id=%s",
                    result.orphaned_comparison_id,
                )
        logger.info("run deleted id=%s artifact_directory=%s", run_id, staged.original)
