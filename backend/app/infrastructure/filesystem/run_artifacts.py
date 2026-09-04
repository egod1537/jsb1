from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from app.domain.artifacts import StoredArtifact
from app.infrastructure.filesystem.atomic import AtomicFileStore


class RunArtifactStore:
    """Confines execution artifacts to the configured data directory."""

    def __init__(self, data_dir: Path, files: AtomicFileStore | None = None) -> None:
        self.root = data_dir.resolve()
        self.files = files or AtomicFileStore()

    def prepare_output(self, relative_directory: str) -> Path:
        output = self._within_root(self.root / relative_directory)
        output.mkdir(parents=True, exist_ok=True)
        return output

    def path(self, output_directory: Path, relative_path: str) -> Path:
        output = self._within_root(output_directory)
        candidate = (output / relative_path).resolve()
        try:
            candidate.relative_to(output)
        except ValueError as exc:
            raise ValueError("Runtime artifact path escapes output directory") from exc
        return candidate

    def is_file(self, path: Path) -> bool:
        return self._within_root(path).is_file()

    def relative_path(self, path: Path) -> str:
        return self._within_root(path).relative_to(self.root).as_posix()

    def resolve(self, relative_path: str) -> Path:
        return self._within_root(self.root / relative_path)

    def metadata(self, kind: str, path: Path) -> StoredArtifact:
        artifact = self._within_root(path)
        digest = hashlib.sha256()
        size = 0
        with artifact.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                digest.update(chunk)
                size += len(chunk)
        return StoredArtifact(
            kind=kind,
            relative_path=self.relative_path(artifact),
            sha256=digest.hexdigest(),
            size_bytes=size,
        )

    def read_text(self, path: Path) -> str:
        return self._within_root(path).read_text(encoding="utf-8")

    def write_json(self, path: Path, payload: Any) -> None:
        self.files.write_text(
            self._within_root(path),
            json.dumps(payload, indent=2, allow_nan=False),
        )

    def _within_root(self, path: Path) -> Path:
        candidate = path.resolve()
        try:
            candidate.relative_to(self.root)
        except ValueError as exc:
            raise ValueError(
                "Run artifact path escapes configured data directory"
            ) from exc
        return candidate
