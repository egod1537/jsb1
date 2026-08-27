from __future__ import annotations

from pathlib import Path
from typing import Any

from app.domain.models import Artifact


class UnsafeArtifactPath(RuntimeError):
    pass


class ArtifactService:
    def __init__(self, data_dir: Path) -> None:
        self.data_dir = data_dir.resolve()

    def public(self, row: dict[str, Any]) -> Artifact:
        path = self.resolve(row["path"])
        return Artifact(
            id=row["id"],
            run_id=row["run_id"],
            kind=row["kind"],
            filename=path.name,
            download_url=f"/api/runs/{row['run_id']}/artifacts/{row['kind']}",
        )

    def resolve(self, relative_path: str) -> Path:
        path = (self.data_dir / relative_path).resolve()
        try:
            path.relative_to(self.data_dir)
        except ValueError as exc:
            raise UnsafeArtifactPath("artifact is outside JSB1_DATA_DIR") from exc
        return path

