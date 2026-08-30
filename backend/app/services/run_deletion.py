from __future__ import annotations

import logging
import shutil
from pathlib import Path

from app.domain.models import RunStatus
from app.repositories.runs import RunRepository


logger = logging.getLogger(__name__)


class ActiveRunDeletionNotAllowed(RuntimeError):
    pass


class UnsafeRunDirectory(RuntimeError):
    pass


class RunDeletionService:
    """Deletes terminal Run-owned state without touching shared runtime/build data."""

    def __init__(self, runs: RunRepository, runs_dir: Path) -> None:
        self.runs = runs
        self.runs_dir = runs_dir.resolve()

    def delete(self, run_id: int) -> None:
        run = self.runs.get(run_id)
        if run.status in {RunStatus.QUEUED, RunStatus.RUNNING}:
            raise ActiveRunDeletionNotAllowed(
                f"run {run_id} is {run.status.value} and cannot be deleted"
            )

        run_directory = self._run_directory(run_id)
        if run_directory.is_symlink():
            raise UnsafeRunDirectory(
                f"run {run_id} artifact path is not a safe directory"
            )
        if run_directory.exists():
            if not run_directory.is_dir():
                raise UnsafeRunDirectory(
                    f"run {run_id} artifact path is not a safe directory"
                )
            shutil.rmtree(run_directory)

        # The status predicate protects against a stale caller even though terminal
        # Runs cannot normally transition back into the active lifecycle.
        if not self.runs.delete_terminal(run_id):
            try:
                current = self.runs.get(run_id)
            except KeyError:
                raise
            raise ActiveRunDeletionNotAllowed(
                f"run {run_id} is {current.status.value} and cannot be deleted"
            )
        logger.info("run deleted id=%s artifact_directory=%s", run_id, run_directory)

    def _run_directory(self, run_id: int) -> Path:
        candidate = self.runs_dir / f"{run_id:06d}"
        try:
            candidate.parent.resolve().relative_to(self.runs_dir)
        except ValueError as exc:
            raise UnsafeRunDirectory("run artifact directory escapes runs root") from exc
        return candidate
