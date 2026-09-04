from __future__ import annotations

import shutil
import uuid
from dataclasses import dataclass
from pathlib import Path


class UnsafeDirectory(RuntimeError):
    pass


@dataclass(frozen=True)
class StagedDirectoryDeletion:
    original: Path
    staged: Path | None


class RunDirectoryStore:
    """Delete only concrete per-run directories beneath the configured root."""

    def __init__(self, runs_root: Path) -> None:
        self.runs_root = runs_root.resolve()

    def delete(self, run_id: int) -> Path:
        staged = self.stage(run_id)
        self.commit(staged)
        return staged.original

    def stage(self, run_id: int) -> StagedDirectoryDeletion:
        candidate = self._safe_directory(
            self.runs_root, self.runs_root / f"{run_id:06d}"
        )
        if not candidate.exists():
            return StagedDirectoryDeletion(candidate, None)
        staged = self.runs_root / f".deleting-{run_id:06d}-{uuid.uuid4().hex}"
        candidate.replace(staged)
        return StagedDirectoryDeletion(candidate, staged)

    @staticmethod
    def commit(deletion: StagedDirectoryDeletion) -> None:
        if deletion.staged is not None and deletion.staged.exists():
            shutil.rmtree(deletion.staged)

    @staticmethod
    def rollback(deletion: StagedDirectoryDeletion) -> None:
        if deletion.staged is None or not deletion.staged.exists():
            return
        if deletion.original.exists():
            raise UnsafeDirectory("cannot restore staged run directory")
        deletion.staged.replace(deletion.original)

    def delete_comparison(self, comparison_id: int) -> Path:
        root = (self.runs_root.parent / "comparisons").resolve()
        candidate = self._safe_directory(root, root / f"{comparison_id:06d}")
        if candidate.exists():
            shutil.rmtree(candidate)
        return candidate

    @staticmethod
    def _safe_directory(root: Path, candidate: Path) -> Path:
        try:
            candidate.parent.resolve().relative_to(root)
        except ValueError as exc:
            raise UnsafeDirectory(
                "artifact directory escapes its managed root"
            ) from exc
        if candidate.is_symlink() or (candidate.exists() and not candidate.is_dir()):
            raise UnsafeDirectory("artifact path is not a safe directory")
        return candidate
