from __future__ import annotations

from pathlib import Path
from typing import Any

from app.domain.models import Artifact
from app.infrastructure.filesystem import LocalArtifactStore, UnsafeArtifactPath


class ArtifactService:
    """Application-facing artifact lookup and public metadata projection."""

    def __init__(self, data_dir: Path, store: LocalArtifactStore | None = None) -> None:
        self.store = store or LocalArtifactStore(data_dir)

    def public(self, row: dict[str, Any]) -> Artifact:
        path = self.store.resolve(row["path"])
        return Artifact(
            id=row["id"],
            run_id=row["run_id"],
            kind=row["kind"],
            filename=path.name,
            download_url=f"/api/runs/{row['run_id']}/artifacts/{row['kind']}",
            sha256=row.get("sha256"),
            size_bytes=row.get("size_bytes"),
        )

    def resolve(self, relative_path: str) -> Path:
        return self.store.resolve(relative_path)

    def require_file(self, relative_path: str) -> Path:
        return self.store.require_file(relative_path)

    def read_text(self, relative_path: str) -> str:
        return self.store.read_text(relative_path)

    def read_managed_text(self, path: str | Path) -> tuple[str, str]:
        return self.store.read_managed_text(path)


__all__ = ["ArtifactService", "UnsafeArtifactPath"]
