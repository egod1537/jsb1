from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from app.infrastructure.filesystem import AtomicFileStore


class UnsafeSnapshotPath(ValueError):
    pass


@dataclass(frozen=True)
class ScenarioSnapshot:
    absolute_path: Path
    relative_path: str


class ScenarioSnapshotService:
    """Persist immutable scenario provenance beneath the JSB1 data root."""

    def __init__(
        self, data_root: Path, files: AtomicFileStore | None = None
    ) -> None:
        self.data_root = data_root.resolve()
        self.files = files or AtomicFileStore()

    def for_run(self, run_id: int, yaml_text: str) -> ScenarioSnapshot:
        return self._write(f"runs/{run_id:06d}/scenario.yaml", yaml_text)

    def for_comparison(self, comparison_id: int, yaml_text: str) -> ScenarioSnapshot:
        return self._write(
            f"comparisons/{comparison_id:06d}/scenario.yaml", yaml_text
        )

    def copy_to_run(self, run_id: int, yaml_text: str) -> ScenarioSnapshot:
        return self.for_run(run_id, yaml_text)

    def parameters_for_run(self, run_id: int, yaml_text: str) -> ScenarioSnapshot:
        return self._write(f"runs/{run_id:06d}/parameters.yaml", yaml_text)

    def _write(self, relative_path: str, yaml_text: str) -> ScenarioSnapshot:
        destination = (self.data_root / relative_path).resolve()
        try:
            destination.relative_to(self.data_root)
        except ValueError as exc:
            raise UnsafeSnapshotPath("scenario snapshot escapes data root") from exc
        self.files.write_text(destination, yaml_text)
        return ScenarioSnapshot(destination, relative_path)
