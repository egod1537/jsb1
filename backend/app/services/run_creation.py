from __future__ import annotations

import asyncio
from dataclasses import dataclass

from app.domain.models import RunStatus
from app.domain.errors import SnapshotWriteFailed
from app.repositories.instances import InstanceRepository
from app.repositories.runs import RunRepository, utc_now
from app.services.build_manager import BuildManager
from app.services.controller_parameters import RuntimeControllerParameterService
from app.services.repository_manager import RepositoryManager
from app.services.runtime_variants import RuntimeVariantService
from app.services.scenario_snapshots import ScenarioSnapshotService
from app.services.scenarios import ScenarioService
from app.services.execution_pipeline import RUN_PIPELINE, ExecutionPipelineRecorder
from app.workers.dispatch import BuildDispatcher, RunDispatcher


@dataclass(frozen=True)
class CreateRunCommand:
    scenario: str
    scenario_source: str = "bundled"
    variant: str | None = None
    legacy_autopilot: str | None = None
    repository_id: int | None = None
    branch: str | None = None
    commit_sha: str | None = None
    build_id: int | None = None
    controller_parameters: dict[str, float] | None = None


@dataclass(frozen=True)
class CreateRunResult:
    id: int
    status: str
    repository_id: int | None
    branch: str | None
    build_id: int | None
    build_status: str | None
    build_reused: bool
    commit_sha: str | None
    execution_variant: str


class RunCreationService:
    """Create one immutable Run and hand only its id to the schedulers."""

    def __init__(
        self,
        runs: RunRepository,
        scenarios: ScenarioService,
        repositories: RepositoryManager,
        builds: BuildManager,
        variants: RuntimeVariantService,
        controller_parameters: RuntimeControllerParameterService,
        snapshots: ScenarioSnapshotService,
        instances: InstanceRepository,
        build_scheduler: BuildDispatcher,
        run_scheduler: RunDispatcher,
    ) -> None:
        self.runs = runs
        self.scenarios = scenarios
        self.repositories = repositories
        self.builds = builds
        self.variants = variants
        self.controller_parameters = controller_parameters
        self.snapshots = snapshots
        self.instances = instances
        self.build_scheduler = build_scheduler
        self.run_scheduler = run_scheduler

    async def create(self, command: CreateRunCommand) -> CreateRunResult:
        if command.controller_parameters and command.branch is None:
            raise ValueError(
                "Controller parameters require a branch-based immutable Runtime revision"
            )
        scenario = self.scenarios.load(command.scenario, command.scenario_source)
        legacy_execution_variant = (
            command.variant
            or command.legacy_autopilot
            or scenario.legacy_autopilot
        )
        if command.branch is None and legacy_execution_variant is None:
            raise ValueError("Execution variant is required")
        execution_variant = legacy_execution_variant or "compare"
        execution_mode = "single"
        run_variants = [execution_variant]

        repository_id: int | None = None
        branch: str | None = None
        commit_sha = command.commit_sha
        build_id = command.build_id
        build_reused = False
        build_status: str | None = None
        effective_parameters: dict[str, float] = {}
        parameter_overrides: dict[str, float] = {}
        variant_parameters: dict[str, dict[str, float]] = {}
        if command.branch is not None:
            runtime = (
                await asyncio.to_thread(self.repositories.runtime_repository)
                if command.repository_id is None
                else None
            )
            repository_id = runtime.id if runtime is not None else command.repository_id
            assert repository_id is not None
            await asyncio.to_thread(self.repositories.fetch, repository_id)
            revision = await asyncio.to_thread(
                self.repositories.resolve_branch, repository_id, command.branch
            )
            worktree = await asyncio.to_thread(
                self.repositories.prepare_worktree,
                repository_id,
                revision.commit_sha,
            )
            await asyncio.to_thread(
                self.scenarios.validate_runtime_contract, scenario, worktree
            )
            capability = await asyncio.to_thread(
                self.variants.capabilities, worktree
            )
            if not capability.authoritative or capability.mode != "compare":
                raise ValueError(
                    "Selected JSB0 revision does not declare compare-only headless execution"
                )
            if len(capability.variants) < 2:
                raise ValueError(
                    "Compare-only headless execution requires at least two variants"
                )
            execution_mode = capability.mode
            run_variants = list(capability.variants)
            execution_variant = "compare"
            parameter_resolution = await asyncio.to_thread(
                self.controller_parameters.resolve_for_variants,
                worktree,
                run_variants,
                command.controller_parameters or {},
                scenario.controller_parameters,
            )
            effective_parameters = parameter_resolution.effective
            parameter_overrides = parameter_resolution.overrides
            variant_parameters = parameter_resolution.by_variant or {}
            build, build_reused = await asyncio.to_thread(
                self.builds.request_resolved, revision
            )
            branch = command.branch
            commit_sha = revision.commit_sha
            build_id = build.id
            build_status = build.status.value
        elif command.build_id is not None:
            build, _ = self.builds.require_runnable(command.build_id)
            if commit_sha is not None and commit_sha != build.commit_sha:
                raise ValueError("commit_sha does not match selected build")
            repository_id = build.repository_id
            commit_sha = build.commit_sha

        run = self.runs.create(
            repository_id=repository_id,
            branch=branch,
            build_id=build_id,
            commit_sha=commit_sha,
            scenario_name=scenario.name,
            scenario_type=scenario.scenario_type,
            scenario_path=str(scenario.path),
            autopilot=execution_variant,
            execution_variant=execution_variant,
            scenario_id=command.scenario,
            scenario_source=scenario.source,
            scenario_sha256=scenario.sha256,
            controller_parameters=effective_parameters,
            controller_parameter_overrides=parameter_overrides,
            execution_mode=execution_mode,
            variants=run_variants,
            variant_parameters=variant_parameters,
        )
        pipeline = ExecutionPipelineRecorder(self.runs, run.id, RUN_PIPELINE)
        pipeline.initialize()
        pipeline.success("resolve_scenario")
        if command.branch is not None:
            pipeline.success("resolve_runtime_revision", commit_sha or "")
            pipeline.success("validate_contract")
            pipeline.success(
                "resolve_build",
                f"Build #{build_id}{' reused' if build_reused else ' reserved'}",
            )
        else:
            pipeline.skipped(
                "resolve_runtime_revision", "Legacy immutable revision input"
            )
            pipeline.skipped(
                "validate_contract", "No Runtime worktree was selected"
            )
            if build_id is None:
                pipeline.skipped("resolve_build", "Legacy runner path")
            else:
                pipeline.success("resolve_build", f"Selected build #{build_id}")
        pipeline.running("freeze_scenario")
        parameter_snapshot = None
        try:
            snapshot = self.snapshots.for_run(run.id, scenario.yaml_text)
            if parameter_overrides:
                parameter_snapshot = self.snapshots.parameters_for_run(
                    run.id,
                    self.controller_parameters.serialize(effective_parameters),
                )
        except OSError as exc:
            pipeline.failed(
                "freeze_scenario", "Could not persist immutable execution snapshots"
            )
            self.runs.transition(
                run.id,
                expected=[RunStatus.QUEUED],
                status=RunStatus.FAILED,
                finished_at=utc_now(),
                error_message="could not persist immutable execution snapshots",
            )
            raise SnapshotWriteFailed(
                "Could not persist immutable execution snapshots"
            ) from exc
        pipeline.success("freeze_scenario", snapshot.relative_path)
        self.runs.set_scenario_path(run.id, str(snapshot.absolute_path))
        self.runs.set_output_directory(run.id, f"runs/{run.id:06d}")
        self.runs.upsert_artifact(run.id, "scenario", snapshot.relative_path)
        if parameter_snapshot is not None:
            self.runs.upsert_artifact(
                run.id, "parameters", parameter_snapshot.relative_path
            )
        if build_id is not None:
            self.instances.create(build_id=build_id, run_id=run.id)
        if branch is not None and not build_reused:
            assert build_id is not None
            self.build_scheduler.submit(build_id)
        self.run_scheduler.submit(
            run.id,
            wait_for_build_id=build_id if branch is not None else None,
        )
        return CreateRunResult(
            id=run.id,
            status=RunStatus.QUEUED.value,
            repository_id=repository_id,
            branch=branch,
            build_id=build_id,
            build_status=build_status,
            build_reused=build_reused,
            commit_sha=commit_sha,
            execution_variant=execution_variant,
        )
