from __future__ import annotations

from dataclasses import dataclass
from pathlib import PurePosixPath


@dataclass(frozen=True)
class StoredArtifact:
    """Metadata persisted by SQLite for bytes owned by an artifact store."""

    kind: str
    relative_path: str
    sha256: str
    size_bytes: int

    def __post_init__(self) -> None:
        path = PurePosixPath(self.relative_path)
        if (
            not self.kind
            or not self.relative_path
            or path.is_absolute()
            or ".." in path.parts
            or "\\" in self.relative_path
        ):
            raise ValueError("artifact metadata requires a safe relative path")
        if len(self.sha256) != 64 or any(
            character not in "0123456789abcdef" for character in self.sha256
        ):
            raise ValueError("artifact metadata requires a lowercase SHA-256 digest")
        if self.size_bytes < 0:
            raise ValueError("artifact size must not be negative")
