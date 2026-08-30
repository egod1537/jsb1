from __future__ import annotations

import threading
from pathlib import Path

from app.infrastructure.git.repository import GitOperationError, GitRepositoryAdapter


class WorktreeManager:
    """Materialize immutable Git worktrees, serialized per repository."""

    def __init__(self, root: Path, git: GitRepositoryAdapter) -> None:
        self.root = root.resolve()
        self.root.mkdir(parents=True, exist_ok=True)
        self.git = git
        self._locks_guard = threading.Lock()
        self._locks: dict[int, threading.Lock] = {}

    def prepare(self, repository_id: int, source: Path, commit_sha: str) -> Path:
        destination = (self.root / str(repository_id) / commit_sha).resolve()
        try:
            destination.relative_to(self.root)
        except ValueError as exc:
            raise GitOperationError("worktree path escapes configured root") from exc
        with self._lock_for(repository_id):
            if destination.exists():
                existing = self.git.git(
                    destination, ["rev-parse", "HEAD"], operation="inspect worktree"
                )
                if existing != commit_sha:
                    raise GitOperationError(
                        "existing worktree points to a different commit"
                    )
                return destination
            destination.parent.mkdir(parents=True, exist_ok=True)
            self.git.git(source, ["worktree", "prune"], operation="prune worktrees")
            self.git.git(
                source,
                ["worktree", "add", "--detach", str(destination), commit_sha],
                operation="create worktree",
                timeout=300,
            )
        return destination

    def _lock_for(self, repository_id: int) -> threading.Lock:
        with self._locks_guard:
            return self._locks.setdefault(repository_id, threading.Lock())
