from __future__ import annotations

from pathlib import Path, PurePosixPath


class InvalidRepositoryFilesystemPath(ValueError):
    pass


class RepositoryPathResolver:
    """Translate persisted logical repository paths at the filesystem boundary."""

    def __init__(self, repository_root: Path, worktree_root: Path) -> None:
        self.repository_root = repository_root.resolve()
        self.worktree_root = worktree_root.resolve()
        self.repository_root.mkdir(parents=True, exist_ok=True)
        self.worktree_root.mkdir(parents=True, exist_ok=True)

    def source(self, configured: str) -> Path:
        filesystem_path = Path(configured).expanduser()
        if filesystem_path.is_absolute():
            resolved = filesystem_path.resolve()
            if resolved == Path("/"):
                raise InvalidRepositoryFilesystemPath(
                    "repository path cannot be filesystem root"
                )
            return resolved
        candidate = PurePosixPath(configured)
        if not candidate.parts or ".." in candidate.parts:
            raise InvalidRepositoryFilesystemPath(
                "local_path must not escape repository root"
            )
        return self.under(
            self.repository_root,
            self.repository_root.joinpath(*candidate.parts),
        )

    def stored(self, source: Path) -> str:
        try:
            return source.relative_to(self.repository_root).as_posix()
        except ValueError:
            return str(source)

    @staticmethod
    def under(root: Path, candidate: Path) -> Path:
        resolved = candidate.resolve()
        try:
            resolved.relative_to(root.resolve())
        except ValueError as exc:
            raise InvalidRepositoryFilesystemPath(
                "path escapes configured root"
            ) from exc
        return resolved
