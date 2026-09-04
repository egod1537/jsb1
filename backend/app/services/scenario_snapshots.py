from __future__ import annotations

import hashlib
from dataclasses import dataclass
from pathlib import Path

from app.infrastructure.filesystem import AtomicFileStore


class UnsafeSnapshotPath(ValueError):
    pass


@dataclass(frozen=True)
class ScenarioSnapshot:
    absolute_path: Path
    relative_path: str
    sha256: str
    size_bytes: int


class ScenarioSnapshotService:
    """Persist immutable scenario provenance beneath the JSB1 data root."""

    def __init__(self, data_root: Path, files: AtomicFileStore | None = None) -> None:
        self.data_root = data_root.resolve()
        self.files = files or AtomicFileStore()

    def for_run(
        self, run_id: int, yaml_text: str, artifact_path: str
    ) -> ScenarioSnapshot:
        return self._write(f"runs/{run_id:06d}/{artifact_path}", yaml_text)

    def for_comparison(self, comparison_id: int, yaml_text: str) -> ScenarioSnapshot:
        return self._write(f"comparisons/{comparison_id:06d}/scenario.yaml", yaml_text)

    def copy_to_run(
        self, run_id: int, yaml_text: str, artifact_path: str
    ) -> ScenarioSnapshot:
        return self.for_run(run_id, yaml_text, artifact_path)

    def parameters_for_run(
        self, run_id: int, yaml_text: str, artifact_path: str
    ) -> ScenarioSnapshot:
        return self._write(f"runs/{run_id:06d}/{artifact_path}", yaml_text)

    def _write(self, relative_path: str, yaml_text: str) -> ScenarioSnapshot:
        destination = (self.data_root / relative_path).resolve()
        try:
            destination.relative_to(self.data_root)
        except ValueError as exc:
            raise UnsafeSnapshotPath("scenario snapshot escapes data root") from exc
        content = yaml_text.encode("utf-8")
        self.files.write_bytes(destination, content)
        return ScenarioSnapshot(
            destination,
            relative_path,
            hashlib.sha256(content).hexdigest(),
            len(content),
        )
