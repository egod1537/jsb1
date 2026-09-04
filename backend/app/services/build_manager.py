from __future__ import annotations

import asyncio
import logging
from pathlib import Path

from app.domain.build import Build, BuildStatus
from app.domain.clock import utc_now
from app.domain.repository import Revision
from app.domain.runtime import BuildKey
from app.infrastructure.build import BuildWorkspaceStore, CmakeBuildAdapter
from app.repositories.builds import BuildRepository
from app.services.execution_pipeline import (
    BUILD_PIPELINE,
    ExecutionPipelineRecorder,
)
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
        builder: CmakeBuildAdapter | None = None,
        workspace: BuildWorkspaceStore | None = None,
    ) -> None:
        self.repository = repository
        self.repositories = repositories
        if (
            executable_relative_path.is_absolute()
            or ".." in executable_relative_path.parts
        ):
            raise InvalidRepositoryPath(
                "build executable path must be relative to the build directory"
            )
        self.workspace = workspace or BuildWorkspaceStore(
            build_root, executable_relative_path
        )
        self.build_root = self.workspace.root
        self.executable_relative_path = executable_relative_path
        self.build_jobs = build_jobs
        self.timeout_sec = timeout_sec
        self.builder = builder or CmakeBuildAdapter(
            jobs=build_jobs, timeout_sec=timeout_sec
        )

    def list(self, *, repository_id: int | None, limit: int) -> list[Build]:
        return self.repository.list(repository_id=repository_id, limit=limit)

    def get(self, build_id: int) -> Build:
        return self.repository.get(build_id)

    def public(self, build: Build) -> Build:
        """Project infrastructure paths as workspace-relative API metadata."""
        updates: dict[str, str | None] = {}
        for field in ("build_dir", "executable_path", "stdout_path", "stderr_path"):
            value = getattr(build, field)
            updates[field] = self.workspace.relative(Path(value)) if value else value
        return build.model_copy(update=updates)

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
            if self.workspace.validate_cached(build, key) is not None:
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
        return self.repository.get(build.id).model_copy(
            update={"reused": reused}
        ), reused

    def _ensure_pipeline(self, build: Build, *, reused: bool) -> Build:
        recorder = ExecutionPipelineRecorder(self.repository, build.id, BUILD_PIPELINE)
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
                build.error_message
                or "Build failed before stage history was available",
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
        return self.workspace.paths_for_id(build_id)

    def require_runnable(self, build_id: int) -> tuple[Build, Path]:
        build = self.repository.get(build_id)
        if build.status is not BuildStatus.COMPLETED or not build.executable_path:
            detail = f": {build.error_message}" if build.error_message else ""
            raise RuntimeError(f"build #{build.id} is {build.status.value}{detail}")
        executable = self.workspace.validate_cached(
            build, BuildKey(build.repository_id, build.commit_sha)
        )
        if executable is None:
            raise RuntimeError(
                f"build #{build.id} cache metadata or executable is invalid"
            )
        return build, executable

    def log_path(self, build_id: int, stream: str) -> Path:
        if stream not in {"stdout", "stderr"}:
            raise KeyError(stream)
        build = self.repository.get(build_id)
        return self.workspace.log_path(build, stream)

    async def execute(self, build_id: int) -> None:
        build = self.repository.claim_for_build(build_id, started_at=utc_now())
        if build is None:
            logger.info("build claim skipped id=%s", build_id)
            return
        pipeline = ExecutionPipelineRecorder(self.repository, build_id, BUILD_PIPELINE)
        pipeline.initialize()
        try:
            if (
                next(
                    (
                        stage.status.value
                        for stage in self.repository.get(build_id).stages
                        if stage.id == "fetch_repository"
                    ),
                    "pending",
                )
                == "pending"
            ):
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
            build_dir, stdout_path, stderr_path = self.workspace.prepare(build)
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
                pipeline.running(
                    "configure", "Builder performs configure and compile together"
                )
                await self.builder.build(worktree, build_dir, stdout_path, stderr_path)
                pipeline.success("configure")
                pipeline.success("compile", "Completed by combined builder")
            pipeline.running("verify_artifact")
            executable = self.workspace.executable(build, required=False)
            if executable is None:
                raise FileNotFoundError(
                    f"build completed without {self.executable_relative_path.as_posix()}"
                )
            pipeline.success("verify_artifact")
            pipeline.running("complete")
            self.repository.complete_build(
                build_id,
                executable_path=self.workspace.relative(executable),
                completed_at=utc_now(),
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
            self.repository.fail_build(
                build_id,
                completed_at=utc_now(),
                error_message=message[:4000],
            )
        except Exception:
            logger.exception("could not persist failed build status id=%s", build_id)
