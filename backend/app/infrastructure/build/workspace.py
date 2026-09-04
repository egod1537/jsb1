from __future__ import annotations

import os
from pathlib import Path

from app.domain.build import Build, BuildStatus
from app.domain.errors import UnsafePath
from app.domain.runtime import BuildKey


class BuildWorkspaceStore:
    """Own build-directory layout, log files, and executable verification."""

    def __init__(self, root: Path, executable_relative_path: Path) -> None:
        self.root = root.resolve()
        self.root.mkdir(parents=True, exist_ok=True)
        if (
            executable_relative_path.is_absolute()
            or ".." in executable_relative_path.parts
        ):
            raise UnsafePath(
                "build executable path must be relative to the build directory"
            )
        self.executable_relative_path = executable_relative_path

    def paths_for_id(self, build_id: int) -> tuple[str, str, str]:
        directory = Path(f"{build_id:06d}")
        return (
            directory.as_posix(),
            (directory / "stdout.log").as_posix(),
            (directory / "stderr.log").as_posix(),
        )

    def prepare(self, build: Build) -> tuple[Path, Path, Path]:
        build_dir = self.resolve(Path(build.build_dir))
        build_dir.mkdir(parents=True, exist_ok=True)
        stdout_path = self.resolve(Path(build.stdout_path))
        stderr_path = self.resolve(Path(build.stderr_path))
        stdout_path.write_bytes(b"")
        stderr_path.write_bytes(b"")
        return build_dir, stdout_path, stderr_path

    def executable(self, build: Build, *, required: bool = True) -> Path | None:
        build_dir = self.resolve(Path(build.build_dir))
        candidate = (
            Path(build.executable_path)
            if build.executable_path
            else build_dir / self.executable_relative_path
        )
        path = self.resolve(candidate)
        if not path.is_file() or not os.access(path, os.X_OK):
            if required:
                raise FileNotFoundError("selected build executable is missing")
            return None
        return path

    def validate_cached(self, build: Build, key: BuildKey) -> Path | None:
        """Return the executable only for a complete, internally consistent cache row."""
        if (
            build.status is not BuildStatus.COMPLETED
            or build.repository_id != key.repository_id
            or build.commit_sha != key.commit_sha
            or build.completed_at is None
        ):
            return None
        expected_dir, expected_stdout, expected_stderr = self.paths_for_id(build.id)
        try:
            if self.resolve(Path(build.build_dir)) != self.resolve(Path(expected_dir)):
                return None
            if self.resolve(Path(build.stdout_path)) != self.resolve(
                Path(expected_stdout)
            ):
                return None
            if self.resolve(Path(build.stderr_path)) != self.resolve(
                Path(expected_stderr)
            ):
                return None
            executable = self.executable(build, required=False)
            expected_executable = self.resolve(
                Path(expected_dir) / self.executable_relative_path
            )
        except (OSError, ValueError):
            return None
        return executable if executable == expected_executable else None

    def log_path(self, build: Build, stream: str) -> Path:
        value = build.stdout_path if stream == "stdout" else build.stderr_path
        path = self.resolve(Path(value))
        if not path.is_file():
            raise FileNotFoundError(path)
        return path

    def under(self, candidate: Path) -> Path:
        resolved = candidate.resolve()
        try:
            resolved.relative_to(self.root)
        except ValueError as exc:
            raise UnsafePath("build path escapes configured root") from exc
        return resolved

    def resolve(self, candidate: Path) -> Path:
        return self.under(
            candidate if candidate.is_absolute() else self.root / candidate
        )

    def relative(self, candidate: Path) -> str:
        return self.resolve(candidate).relative_to(self.root).as_posix()
