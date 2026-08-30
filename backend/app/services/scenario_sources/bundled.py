from __future__ import annotations

from pathlib import Path

from app.services.scenario_sources.base import ScenarioObject, validate_object_id


class BundledScenarioSource:
    def __init__(self, root: Path) -> None:
        self.root = root.resolve()

    def list_objects(self) -> list[ScenarioObject]:
        if not self.root.is_dir():
            return []
        return [
            self.stat(path.relative_to(self.root).as_posix())
            for path in sorted(self.root.rglob("*"))
            if path.is_file() and path.suffix.lower() in {".yaml", ".yml"}
        ]

    def fetch(self, object_id: str) -> bytes:
        return self._path(object_id).read_bytes()

    def stat(self, object_id: str) -> ScenarioObject:
        path = self._path(object_id)
        value = path.stat()
        return ScenarioObject(object_id, value.st_size, value.st_mtime)

    def _path(self, object_id: str) -> Path:
        relative = validate_object_id(object_id)
        path = (self.root / Path(*relative.parts)).resolve()
        try:
            path.relative_to(self.root)
        except ValueError as exc:
            raise ValueError("scenario object escapes bundled root") from exc
        if not path.is_file():
            raise FileNotFoundError(object_id)
        return path
