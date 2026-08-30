from __future__ import annotations

import asyncio
import logging
import os
from pathlib import Path

from app.domain.build import Build, BuildStatus
from app.repositories.builds import BuildRepository
from app.repositories.runs import utc_now
from app.domain.repository import Revision
from app.domain.runtime import BuildKey
from app.infrastructure.build import CmakeBuildAdapter
from app.services.repository_manager import InvalidRepositoryPath, RepositoryManager
from app.services.execution_pipeline import (
    BUILD_PIPELINE,
    ExecutionPipelineRecorder,
)


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
        builder: CmakeBuildAdapter | None = None,
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
        self.builder = builder or CmakeBuildAdapter(
            jobs=build_jobs, timeout_sec=timeout_sec
        )

    def request(
        self, repository_id: int, revision: str, *, rebuild: bool = False
    ) -> tuple[Build, bool]:
        resolved = self.repositories.revision(repository_id, revision)
        return self.request_resolved(resolved, rebuild=rebuild)

    def request_resolved(
        self, resolved: Revision, *, rebuild: bool = False
    ) -> tuple[Build, bool]:
        key = BuildKey(resolved.repository_id, resolved.commit_sha)
        build, reused = self.repository.reserve(
            repository_id=key.repository_id,
            commit_sha=key.commit_sha,
            branch=resolved.branch,
            rebuild=rebuild,
            paths_for_id=self._paths_for_id,
        )
        build = self._ensure_pipeline(build, reused=reused)
        if reused and build.status is BuildStatus.COMPLETED:
            if build.executable_path:
                executable = self._under(
                    self.build_root, Path(build.executable_path)
                )
                if executable.is_file():
                    return build.model_copy(update={"reused": True}), True
            # A completed DB row without its immutable executable is not a
            # usable cache entry. Reserve a replacement while still sharing
            # any active replacement another caller may have started.
            build, reused = self.repository.reserve(
                repository_id=key.repository_id,
                commit_sha=key.commit_sha,
                branch=resolved.branch,
                rebuild=True,
                paths_for_id=self._paths_for_id,
            )
            build = self._ensure_pipeline(build, reused=reused)
        return self.repository.get(build.id).model_copy(update={"reused": reused}), reused

    def _ensure_pipeline(self, build: Build, *, reused: bool) -> Build:
        recorder = ExecutionPipelineRecorder(
            self.repository, build.id, BUILD_PIPELINE
        )
        was_empty = not build.stages
        recorder.initialize()
        if not was_empty:
            return self.repository.get(build.id)
        if build.status is BuildStatus.COMPLETED:
            for stage_id, _ in BUILD_PIPELINE:
                recorder.success(stage_id, "Recovered completed build history")
        elif build.status is BuildStatus.FAILED:
            recorder.failed(
                "fetch_repository",
                build.error_message or "Build failed before stage history was available",
            )
        else:
            recorder.success(
                "fetch_repository",
                "Immutable repository revision resolved"
                if not reused
                else "Shared immutable build reservation",
            )
        return self.repository.get(build.id)

    def _paths_for_id(self, build_id: int) -> tuple[str, str, str]:
        directory = self._under(
            self.build_root, self.build_root / f"{build_id:06d}"
        )
        return (
            str(directory),
            str(directory / "stdout.log"),
            str(directory / "stderr.log"),
        )

    def require_runnable(self, build_id: int) -> tuple[Build, Path]:
        build = self.repository.get(build_id)
        if build.status is not BuildStatus.COMPLETED or not build.executable_path:
            detail = f": {build.error_message}" if build.error_message else ""
            raise RuntimeError(
                f"build #{build.id} is {build.status.value}{detail}"
            )
        build_dir = self._under(self.build_root, Path(build.build_dir))
        executable = self._under(build_dir, Path(build.executable_path))
        if not executable.is_file() or not os.access(executable, os.X_OK):
            raise FileNotFoundError("selected build executable is missing")
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
        pipeline = ExecutionPipelineRecorder(
            self.repository, build_id, BUILD_PIPELINE
        )
        pipeline.initialize()
        try:
            self.repository.transition(
                build_id,
                expected=[BuildStatus.QUEUED],
                status=BuildStatus.RUNNING,
                started_at=utc_now(),
            )
            if next(
                (stage.status.value for stage in self.repository.get(build_id).stages if stage.id == "fetch_repository"),
                "pending",
            ) == "pending":
                pipeline.success(
                    "fetch_repository", "Immutable repository revision available"
                )
            pipeline.running("prepare_worktree")
            worktree = await asyncio.to_thread(
                self.repositories.prepare_worktree,
                build.repository_id,
                build.commit_sha,
            )
            pipeline.success("prepare_worktree")
            build_dir = self._under(self.build_root, Path(build.build_dir))
            build_dir.mkdir(parents=True, exist_ok=True)
            stdout_path = self._under(self.build_root, Path(build.stdout_path))
            stderr_path = self._under(self.build_root, Path(build.stderr_path))
            stdout_path.write_bytes(b"")
            stderr_path.write_bytes(b"")
            configure = getattr(self.builder, "configure", None)
            compile_build = getattr(self.builder, "compile", None)
            if configure is not None and compile_build is not None:
                pipeline.running("configure")
                await configure(worktree, build_dir, stdout_path, stderr_path)
                pipeline.success("configure")
                pipeline.running("compile")
                await compile_build(build_dir, stdout_path, stderr_path)
                pipeline.success("compile")
            else:
                pipeline.running("configure", "Builder performs configure and compile together")
                await self.builder.build(worktree, build_dir, stdout_path, stderr_path)
                pipeline.success("configure")
                pipeline.success("compile", "Completed by combined builder")
            pipeline.running("verify_artifact")
            executable = self._under(
                build_dir, build_dir / self.executable_relative_path
            )
            if not executable.is_file():
                raise FileNotFoundError(
                    f"build completed without {self.executable_relative_path.as_posix()}"
                )
            if not os.access(executable, os.X_OK):
                raise PermissionError("build output is not executable")
            pipeline.success("verify_artifact")
            pipeline.running("complete")
            self.repository.transition(
                build_id,
                expected=[BuildStatus.RUNNING],
                status=BuildStatus.COMPLETED,
                executable_path=str(executable),
                completed_at=utc_now(),
                error_message=None,
            )
            pipeline.success("complete")
        except asyncio.CancelledError:
            pipeline.fail_current("execution worker stopped while build was active")
            self._fail(build_id, "execution worker stopped while build was active")
            raise
        except Exception as exc:
            logger.exception("build failed id=%s", build_id)
            pipeline.fail_current(str(exc))
            self._fail(build_id, str(exc))

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
