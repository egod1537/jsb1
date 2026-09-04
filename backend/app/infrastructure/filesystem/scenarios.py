from __future__ import annotations

import os
import tempfile
from pathlib import Path, PurePosixPath

from app.domain.scenario_source import (
    ScenarioObject,
    ScenarioSourceType,
    validate_object_id,
)
from app.repositories.scenario_catalog import ScenarioCatalogRepository


class UnsafeManagedScenarioPath(RuntimeError):
    pass


class DirectoryScenarioSource:
    """Read-only bundled or managed scenario directory adapter."""

    def __init__(
        self,
        root: Path,
        source_type: ScenarioSourceType = ScenarioSourceType.BUNDLED,
    ) -> None:
        if source_type is ScenarioSourceType.REMOTE:
            raise ValueError("remote scenarios require a catalog-backed source")
        self.root = root.resolve()
        self._source_type = source_type

    @property
    def source_type(self) -> ScenarioSourceType:
        return self._source_type

    def list(self) -> list[ScenarioObject]:
        if not self.root.is_dir():
            return []
        result: list[ScenarioObject] = []
        for path in sorted(self.root.rglob("*")):
            if not path.is_file() or path.suffix.lower() not in {".yaml", ".yml"}:
                continue
            value = path.stat()
            result.append(
                ScenarioObject(
                    path.relative_to(self.root).as_posix(),
                    value.st_size,
                    value.st_mtime,
                )
            )
        return result

    def read(self, object_id: str) -> bytes:
        return self.path(object_id).read_bytes()

    def path(self, object_id: str) -> Path:
        relative = validate_object_id(object_id)
        candidate = self.root.joinpath(*relative.parts).resolve()
        try:
            candidate.relative_to(self.root)
        except ValueError as exc:
            raise ValueError("scenario object escapes source root") from exc
        if not candidate.is_file():
            raise FileNotFoundError(object_id)
        return candidate


class CatalogCachedScenarioSource:
    """Read active, validated remote bytes selected by the catalog repository."""

    def __init__(self, root: Path, catalog: ScenarioCatalogRepository) -> None:
        self.root = root.resolve()
        self.catalog = catalog

    @property
    def source_type(self) -> ScenarioSourceType:
        return ScenarioSourceType.REMOTE

    def list(self) -> list[ScenarioObject]:
        return [
            ScenarioObject(str(row["scenario_id"]))
            for row in self.catalog.list_valid("sftp")
        ]

    def read(self, object_id: str) -> bytes:
        return self.path(object_id).read_bytes()

    def path(self, object_id: str) -> Path:
        validate_object_id(object_id)
        row = self.catalog.get("sftp", object_id)
        if (
            row is None
            or not row.get("active")
            or not row.get("valid")
            or not row.get("cache_path")
        ):
            raise FileNotFoundError(object_id)
        stored = Path(str(row["cache_path"]))
        candidates = [stored] if stored.is_absolute() else [
            self.root / stored,
            self.root.parent.parent / stored,
        ]
        for candidate in candidates:
            resolved = candidate.resolve()
            try:
                resolved.relative_to(self.root)
            except ValueError:
                continue
            if resolved.is_file():
                return resolved
        raise FileNotFoundError("validated scenario cache is missing")


class ManagedScenarioStore:
    """Atomically create, but never overwrite, one managed scenario file."""

    def __init__(self, root: Path) -> None:
        self.root = root.resolve()

    def create(self, relative: PurePosixPath, content: str) -> Path:
        self.root.mkdir(parents=True, exist_ok=True)
        parent = self.root
        for part in relative.parts[:-1]:
            child = parent / part
            if child.is_symlink():
                raise UnsafeManagedScenarioPath(
                    "scenario path contains a symbolic link"
                )
            child.mkdir(exist_ok=True)
            try:
                child.resolve().relative_to(self.root)
            except ValueError as exc:
                raise UnsafeManagedScenarioPath(
                    "scenario path escapes the managed root"
                ) from exc
            parent = child
        target = parent / relative.name
        if target.exists():
            raise FileExistsError(target)

        temporary_path: Path | None = None
        try:
            descriptor, temporary_name = tempfile.mkstemp(
                dir=target.parent,
                prefix=f".{target.name}.",
                suffix=".tmp",
            )
            temporary_path = Path(temporary_name)
            with os.fdopen(
                descriptor, "w", encoding="utf-8", newline="\n"
            ) as stream:
                stream.write(content)
                stream.flush()
                os.fsync(stream.fileno())
            os.link(temporary_path, target)
        finally:
            if temporary_path is not None:
                temporary_path.unlink(missing_ok=True)
        return target
