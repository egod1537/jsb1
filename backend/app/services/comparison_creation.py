from __future__ import annotations

import asyncio
from dataclasses import dataclass

from app.domain.artifacts import StoredArtifact
from app.domain.clock import utc_now
from app.domain.errors import SnapshotWriteFailed
from app.domain.execution import FrozenRunPreparation
from app.domain.models import Comparison
from app.repositories.comparisons import ComparisonRepository
from app.repositories.instances import InstanceRepository
from app.repositories.runs import RunRepository
from app.services.build_manager import BuildManager
from app.services.execution_pipeline import RUN_PIPELINE, ExecutionPipelineRecorder
from app.services.ports import BuildDispatcher, RunDispatcher
from app.services.repository_manager import RepositoryManager
from app.services.runtime_contract import RuntimeContractReader
from app.services.runtime_variants import RuntimeVariantService
from app.services.scenario_snapshots import ScenarioSnapshotService
from app.services.scenarios import ScenarioService


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
        contract_reader: RuntimeContractReader | None = None,
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
        self.contract_reader = contract_reader or RuntimeContractReader()

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
        bundle = None
        if self.contract_reader.is_indexed(worktree):
            bundle = await asyncio.to_thread(
                self.contract_reader.load_bundle,
                worktree,
                repository_id=runtime.id,
                commit_sha=revision.commit_sha,
            )
        await asyncio.to_thread(
            self.scenarios.validate_runtime_contract,
            scenario,
            worktree,
            reader=self.contract_reader,
        )
        supported = (
            list(bundle.variants)
            if bundle is not None
            else await asyncio.to_thread(self.variants.variants, worktree)
        )
        if bundle is not None and bundle.capabilities.mode != "single":
            raise ValueError(
                "Selected JSB0 execution mode does not support independent variant Runs"
            )
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
            scenario_path="pending",
            repository_id=runtime.id,
            branch=command.branch,
            commit_sha=revision.commit_sha,
            build_id=build.id,
            variants=command.variants,
            contract_version=bundle.version if bundle is not None else None,
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
            artifact_manifest = (
                bundle.artifact_manifest
                if bundle is not None
                else self.contract_reader.load_artifact_manifest(worktree)
            )
            comparison_snapshot = self.snapshots.for_comparison(
                comparison.id, scenario.yaml_text
            )
            preparations: list[FrozenRunPreparation] = []
            for run_id in run_ids:
                snapshot = self.snapshots.copy_to_run(
                    run_id,
                    scenario.yaml_text,
                    artifact_manifest.path_for("scenario_snapshot"),
                )
                preparations.append(
                    FrozenRunPreparation(
                        run_id=run_id,
                        scenario_path=snapshot.relative_path,
                        scenario_sha256=snapshot.sha256,
                        output_directory=f"runs/{run_id:06d}",
                        parameter_snapshot_path=None,
                        parameter_snapshot_sha256=None,
                        artifacts=(
                            StoredArtifact(
                                "scenario",
                                snapshot.relative_path,
                                snapshot.sha256,
                                snapshot.size_bytes,
                            ),
                        ),
                    )
                )
            self.comparisons.finalize_preparation(
                comparison.id,
                scenario_path=comparison_snapshot.relative_path,
                build_id=build.id,
                runs=preparations,
            )
        except Exception as exc:
            for run_id, pipeline in zip(run_ids, pipelines, strict=True):
                try:
                    pipeline.failed(
                        "freeze_scenario",
                        "Could not persist immutable scenario snapshot",
                    )
                    self.runs.fail_run(
                        run_id,
                        finished_at=utc_now(),
                        error_message="could not persist immutable scenario snapshot",
                    )
                except Exception:  # noqa: BLE001, S110 - preserve original failure
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
