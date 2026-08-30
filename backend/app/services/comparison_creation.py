from __future__ import annotations

import asyncio
from dataclasses import dataclass

from app.domain.models import Comparison, RunStatus
from app.domain.errors import SnapshotWriteFailed
from app.repositories.comparisons import ComparisonRepository
from app.repositories.instances import InstanceRepository
from app.repositories.runs import RunRepository, utc_now
from app.services.build_manager import BuildManager
from app.services.repository_manager import RepositoryManager
from app.services.runtime_variants import RuntimeVariantService
from app.services.scenario_snapshots import ScenarioSnapshotService
from app.services.scenarios import ScenarioService
from app.services.execution_pipeline import RUN_PIPELINE, ExecutionPipelineRecorder
from app.workers.dispatch import BuildDispatcher, RunDispatcher


@dataclass(frozen=True)
class CreateComparisonCommand:
    scenario: str
    scenario_source: str
    branch: str
    variants: tuple[str, ...]


class ComparisonCreationService:
    """Freeze one revision/scenario/build before creating child Runs."""

    def __init__(
        self,
        comparisons: ComparisonRepository,
        runs: RunRepository,
        scenarios: ScenarioService,
        repositories: RepositoryManager,
        builds: BuildManager,
        variants: RuntimeVariantService,
        snapshots: ScenarioSnapshotService,
        instances: InstanceRepository,
        build_scheduler: BuildDispatcher,
        run_scheduler: RunDispatcher,
    ) -> None:
        self.comparisons = comparisons
        self.runs = runs
        self.scenarios = scenarios
        self.repositories = repositories
        self.builds = builds
        self.variants = variants
        self.snapshots = snapshots
        self.instances = instances
        self.build_scheduler = build_scheduler
        self.run_scheduler = run_scheduler

    async def create(self, command: CreateComparisonCommand) -> Comparison:
        scenario = self.scenarios.load(command.scenario, command.scenario_source)
        runtime = await asyncio.to_thread(self.repositories.runtime_repository)
        await asyncio.to_thread(self.repositories.fetch, runtime.id)
        revision = await asyncio.to_thread(
            self.repositories.resolve_branch, runtime.id, command.branch
        )
        worktree = await asyncio.to_thread(
            self.repositories.prepare_worktree, runtime.id, revision.commit_sha
        )
        await asyncio.to_thread(
            self.scenarios.validate_runtime_contract, scenario, worktree
        )
        supported = await asyncio.to_thread(self.variants.variants, worktree)
        unsupported = [item for item in command.variants if item not in supported]
        if unsupported:
            raise ValueError(f"Unsupported execution variant: {unsupported[0]}")
        build, build_reused = await asyncio.to_thread(
            self.builds.request_resolved, revision
        )
        comparison, run_ids = self.comparisons.create_with_runs(
            scenario_id=command.scenario,
            scenario_source=scenario.source,
            scenario_name=scenario.name,
            scenario_type=scenario.scenario_type,
            scenario_sha256=scenario.sha256 or "",
            scenario_path=str(scenario.path),
            repository_id=runtime.id,
            branch=command.branch,
            commit_sha=revision.commit_sha,
            build_id=build.id,
            variants=command.variants,
        )
        pipelines = [
            ExecutionPipelineRecorder(self.runs, run_id, RUN_PIPELINE)
            for run_id in run_ids
        ]
        for pipeline in pipelines:
            pipeline.initialize()
            pipeline.success("resolve_scenario")
            pipeline.success("resolve_runtime_revision", revision.commit_sha)
            pipeline.success("validate_contract")
            pipeline.success(
                "resolve_build",
                f"Build #{build.id}{' reused' if build_reused else ' reserved'}",
            )
            pipeline.running("freeze_scenario")
        try:
            comparison_snapshot = self.snapshots.for_comparison(
                comparison.id, scenario.yaml_text
            )
            self.comparisons.set_scenario_path(
                comparison.id, str(comparison_snapshot.absolute_path)
            )
            for run_id in run_ids:
                snapshot = self.snapshots.copy_to_run(run_id, scenario.yaml_text)
                self.runs.set_scenario_path(run_id, str(snapshot.absolute_path))
                self.runs.set_output_directory(run_id, f"runs/{run_id:06d}")
                self.runs.upsert_artifact(run_id, "scenario", snapshot.relative_path)
                self.instances.create(build_id=build.id, run_id=run_id)
        except OSError as exc:
            for run_id, pipeline in zip(run_ids, pipelines, strict=True):
                try:
                    pipeline.failed(
                        "freeze_scenario",
                        "Could not persist immutable scenario snapshot",
                    )
                    self.runs.transition(
                        run_id,
                        expected=[RunStatus.QUEUED],
                        status=RunStatus.FAILED,
                        finished_at=utc_now(),
                        error_message="could not persist immutable scenario snapshot",
                    )
                except Exception:
                    pass
            raise SnapshotWriteFailed(
                "Could not persist immutable scenario snapshot"
            ) from exc
        for pipeline in pipelines:
            pipeline.success("freeze_scenario")
        if not build_reused:
            self.build_scheduler.submit(build.id)
        for run_id in run_ids:
            self.run_scheduler.submit(run_id, wait_for_build_id=build.id)
        return self.comparisons.get(comparison.id)
