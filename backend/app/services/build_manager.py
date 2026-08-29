from __future__ import annotations

import asyncio
import logging
import os
from pathlib import Path

from app.domain.build import Build, BuildStatus
from app.repositories.builds import BuildRepository
from app.repositories.runs import utc_now
from app.domain.repository import Revision
from app.services.repository_manager import InvalidRepositoryPath, RepositoryManager


logger = logging.getLogger(__name__)


class BuildManager:
    def __init__(
        self,
        repository: BuildRepository,
        repositories: RepositoryManager,
        build_root: Path,
        *,
        executable_relative_path: Path,
        build_jobs: int,
        timeout_sec: float,
    ) -> None:
        self.repository = repository
        self.repositories = repositories
        self.build_root = build_root.resolve()
        self.build_root.mkdir(parents=True, exist_ok=True)
        if executable_relative_path.is_absolute() or ".." in executable_relative_path.parts:
            raise InvalidRepositoryPath(
                "build executable path must be relative to the build directory"
            )
        self.executable_relative_path = executable_relative_path
        self.build_jobs = build_jobs
        self.timeout_sec = timeout_sec

    def request(
        self, repository_id: int, revision: str, *, rebuild: bool = False
    ) -> tuple[Build, bool]:
        resolved = self.repositories.revision(repository_id, revision)
        return self.request_resolved(resolved, rebuild=rebuild)

    def request_resolved(
        self, resolved: Revision, *, rebuild: bool = False
    ) -> tuple[Build, bool]:
        repository_id = resolved.repository_id
        if not rebuild:
            cached = self.repository.find_completed(repository_id, resolved.commit_sha)
            if cached and cached.executable_path:
                executable = self._under(self.build_root, Path(cached.executable_path))
                if executable.is_file():
                    return cached.model_copy(update={"reused": True}), True
        build = self.repository.create(
            repository_id=repository_id,
            commit_sha=resolved.commit_sha,
            branch=resolved.branch,
            build_dir="",
            stdout_path="",
            stderr_path="",
        )
        directory = self._under(self.build_root, self.build_root / f"{build.id:06d}")
        build = self.repository.set_paths(
            build.id,
            build_dir=str(directory),
            stdout_path=str(directory / "stdout.log"),
            stderr_path=str(directory / "stderr.log"),
        )
        return build, False

    def require_runnable(self, build_id: int) -> tuple[Build, Path]:
        build = self.repository.get(build_id)
        if build.status is not BuildStatus.COMPLETED or not build.executable_path:
            raise RuntimeError("selected build is not completed")
        build_dir = self._under(self.build_root, Path(build.build_dir))
        executable = self._under(build_dir, Path(build.executable_path))
        if not executable.is_file() or not os.access(executable, os.X_OK):
            raise RuntimeError("selected build executable is unavailable")
        return build, executable

    def log_path(self, build_id: int, stream: str) -> Path:
        if stream not in {"stdout", "stderr"}:
            raise KeyError(stream)
        build = self.repository.get(build_id)
        value = build.stdout_path if stream == "stdout" else build.stderr_path
        build_dir = self._under(self.build_root, Path(build.build_dir))
        path = self._under(build_dir, Path(value))
        if not path.is_file():
            raise FileNotFoundError(path)
        return path

    async def execute(self, build_id: int) -> None:
        build = self.repository.get(build_id)
        try:
            self.repository.transition(
                build_id,
                expected=[BuildStatus.QUEUED],
                status=BuildStatus.RUNNING,
                started_at=utc_now(),
            )
            worktree = await asyncio.to_thread(
                self.repositories.prepare_worktree,
                build.repository_id,
                build.commit_sha,
            )
            build_dir = self._under(self.build_root, Path(build.build_dir))
            build_dir.mkdir(parents=True, exist_ok=True)
            stdout_path = self._under(self.build_root, Path(build.stdout_path))
            stderr_path = self._under(self.build_root, Path(build.stderr_path))
            stdout_path.write_bytes(b"")
            stderr_path.write_bytes(b"")
            await self._command(
                ["cmake", "-S", str(worktree), "-B", str(build_dir)],
                stdout_path,
                stderr_path,
            )
            await self._command(
                ["cmake", "--build", str(build_dir), "-j", str(self.build_jobs)],
                stdout_path,
                stderr_path,
            )
            executable = self._under(
                build_dir, build_dir / self.executable_relative_path
            )
            if not executable.is_file():
                raise FileNotFoundError(
                    f"build completed without {self.executable_relative_path.as_posix()}"
                )
            if not os.access(executable, os.X_OK):
                raise PermissionError("build output is not executable")
            self.repository.transition(
                build_id,
                expected=[BuildStatus.RUNNING],
                status=BuildStatus.COMPLETED,
                executable_path=str(executable),
                completed_at=utc_now(),
                error_message=None,
            )
        except asyncio.CancelledError:
            self._fail(build_id, "backend stopped while build was active")
            raise
        except Exception as exc:
            logger.exception("build failed id=%s", build_id)
            self._fail(build_id, str(exc))

    async def _command(
        self, command: list[str], stdout_path: Path, stderr_path: Path
    ) -> None:
        logger.info("starting build command=%r", command)
        with stdout_path.open("ab") as stdout, stderr_path.open("ab") as stderr:
            process = await asyncio.create_subprocess_exec(
                *command, stdout=stdout, stderr=stderr, env=os.environ.copy()
            )
            try:
                await asyncio.wait_for(process.wait(), timeout=self.timeout_sec)
            except TimeoutError as exc:
                process.terminate()
                try:
                    await asyncio.wait_for(process.wait(), timeout=5)
                except TimeoutError:
                    process.kill()
                    await process.wait()
                raise TimeoutError(
                    f"build command exceeded timeout of {self.timeout_sec:g} seconds"
                ) from exc
            except asyncio.CancelledError:
                process.terminate()
                try:
                    await asyncio.wait_for(process.wait(), timeout=5)
                except TimeoutError:
                    process.kill()
                    await process.wait()
                raise
        if process.returncode != 0:
            raise RuntimeError(f"build command exited with code {process.returncode}")

    def _fail(self, build_id: int, message: str) -> None:
        try:
            self.repository.transition(
                build_id,
                expected=[BuildStatus.QUEUED, BuildStatus.RUNNING],
                status=BuildStatus.FAILED,
                completed_at=utc_now(),
                error_message=message[:4000],
            )
        except Exception:
            logger.exception("could not persist failed build status id=%s", build_id)

    @staticmethod
    def _under(root: Path, candidate: Path) -> Path:
        resolved = candidate.resolve()
        try:
            resolved.relative_to(root.resolve())
        except ValueError as exc:
            raise InvalidRepositoryPath("build path escapes configured root") from exc
        return resolved
