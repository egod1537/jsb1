from __future__ import annotations

from pathlib import Path


class RepositoryProbe:
    """Side-effect-free filesystem readiness probe for a Git checkout."""

    @staticmethod
    def available(path: Path) -> bool:
        git_marker = path / ".git"
        return path.is_dir() and git_marker.exists()
