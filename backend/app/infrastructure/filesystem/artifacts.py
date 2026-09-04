from __future__ import annotations

from pathlib import Path


class UnsafeArtifactPath(RuntimeError):
    pass


class LocalArtifactStore:
    """Own path confinement and file reads beneath the JSB1 data root."""

    def __init__(self, data_dir: Path) -> None:
        self.data_dir = data_dir.resolve()

    def resolve(self, relative_path: str) -> Path:
        return self._under_root(self.data_dir / relative_path)

    def relative(self, path: Path) -> str:
        return self._under_root(path).relative_to(self.data_dir).as_posix()

    def read_text(self, relative_path: str) -> str:
        return self.resolve(relative_path).read_text(encoding="utf-8")

    def read_managed_text(self, path: str | Path) -> tuple[str, str]:
        candidate = Path(path)
        resolved = self._under_root(
            candidate if candidate.is_absolute() else self.data_dir / candidate
        )
        return (
            resolved.read_text(encoding="utf-8"),
            resolved.relative_to(self.data_dir).as_posix(),
        )

    def require_file(self, relative_path: str) -> Path:
        path = self.resolve(relative_path)
        if not path.is_file():
            raise FileNotFoundError(path)
        return path

    def _under_root(self, path: Path) -> Path:
        resolved = path.resolve()
        try:
            resolved.relative_to(self.data_dir)
        except ValueError as exc:
            raise UnsafeArtifactPath("artifact is outside JSB1_DATA_DIR") from exc
        return resolved
