from __future__ import annotations

import asyncio
import logging
from dataclasses import dataclass
from pathlib import Path

from fastapi import FastAPI

from app.analysis.mcap_reader import McapRunReader
from app.analysis.roll_hold_analyzer import RollHoldAnalyzer
from app.config.settings import Settings
from app.domain.build import BuildStatus
from app.domain.build_info import BuildInfo
from app.domain.errors import RepositoryConflict
from app.domain.models import RunStatus
from app.domain.scenario_source import ScenarioSourceType
from app.infrastructure.execution import ExternalSimulationRunner
from app.infrastructure.filesystem import (
    CatalogCachedScenarioSource,
    DirectoryScenarioSource,
)
from app.infrastructure.scenario import SftpScenarioSource
from app.repositories.builds import BuildRepository
from app.repositories.comparisons import ComparisonRepository
from app.repositories.database import Database
from app.repositories.deployments import DeploymentRepository
from app.repositories.instances import InstanceRepository
from app.repositories.jsb_repository_repository import JsbRepositoryRepository
from app.repositories.runs import RunRepository
from app.repositories.scenario_catalog import ScenarioCatalogRepository
from app.services.artifacts import ArtifactService
from app.services.build_manager import BuildManager
from app.services.build_requests import BuildRequestService
from app.services.comparison_creation import ComparisonCreationService
from app.services.comparison_queries import ComparisonQueryService
from app.services.controller_parameters import RuntimeControllerParameterService
from app.services.deployment_manager import DeploymentManager
from app.services.execution import RunExecutionService
from app.services.execution_pipeline import (
    BUILD_PIPELINE,
    RUN_PIPELINE,
    ExecutionPipelineRecorder,
)
from app.services.health import HealthService
from app.services.ports import BuildDispatcher, RunDispatcher, SimulationRunner
from app.services.repository_manager import (
    GitOperationError,
    InvalidRepositoryPath,
    RepositoryManager,
)
from app.services.run_analysis import RunAnalysisService
from app.services.run_creation import RunCreationService
from app.services.run_deletion import RunDeletionService
from app.services.run_queries import RunQueryService
from app.services.runtime_contract import RuntimeContractReader
from app.services.runtime_variants import RuntimeVariantService
from app.services.scenario_inspection import ScenarioInspectionService
from app.services.scenario_snapshot_queries import ScenarioSnapshotQueryService
from app.services.scenario_snapshots import ScenarioSnapshotService
from app.services.scenario_sync import (
    ScenarioSyncService,
    StableScenarioContractResolver,
)
from app.services.scenario_validator import ScenarioValidator
from app.services.scenario_writes import ScenarioWriteService
from app.services.scenarios import ScenarioService
from app.services.telemetry_processing import RunTelemetryProcessor
from app.services.telemetry_queries import TelemetryQueryService
from app.workers.build_scheduler import InProcessBuildScheduler
from app.workers.dispatch import DurableJobDispatcher
from app.workers.scheduler import InProcessRunScheduler

LOGGER = logging.getLogger(__name__)


@dataclass(frozen=True)
class ApplicationRuntime:
    scheduler: RunDispatcher
    build_scheduler: BuildDispatcher
    deployments: DeploymentManager

    async def shutdown(self) -> None:
        await self.scheduler.shutdown()
        await self.build_scheduler.shutdown()
        await self.deployments.shutdown()


@dataclass(frozen=True)
class RepositoryGraph:
    database: Database
    runs: RunRepository
    comparisons: ComparisonRepository
    repositories: JsbRepositoryRepository
    builds: BuildRepository
    instances: InstanceRepository
    deployments: DeploymentRepository
    scenario_catalog: ScenarioCatalogRepository


@dataclass(frozen=True)
class ExecutionGraph:
    repositories: RepositoryManager
    builds: BuildManager
    deployments: DeploymentManager
    contract_reader: RuntimeContractReader
    telemetry_reader: McapRunReader
    build_scheduler: BuildDispatcher
    run_scheduler: RunDispatcher


@dataclass(frozen=True)
class ScenarioGraph:
    validator: ScenarioValidator
    compatibility: StableScenarioContractResolver
    sync: ScenarioSyncService
    scenarios: ScenarioService
    writer: ScenarioWriteService
    inspector: ScenarioInspectionService


def create_repositories(settings: Settings) -> RepositoryGraph:
    migrations = Path(__file__).resolve().parents[1] / "migrations"
    database = Database(settings.resolved_database_path, migrations)
    database.initialize()
    return RepositoryGraph(
        database=database,
        runs=RunRepository(database),
        comparisons=ComparisonRepository(database),
        repositories=JsbRepositoryRepository(database),
        builds=BuildRepository(database),
        instances=InstanceRepository(database),
        deployments=DeploymentRepository(database),
        scenario_catalog=ScenarioCatalogRepository(database),
    )


def create_execution_services(
    settings: Settings,
    graph: RepositoryGraph,
    runner_override: SimulationRunner | None,
) -> ExecutionGraph:
    repositories = RepositoryManager(
        graph.repositories,
        settings.resolved_repository_root,
        settings.resolved_worktree_root,
    )
    builds = BuildManager(
        graph.builds,
        repositories,
        settings.resolved_build_root,
        executable_relative_path=settings.build_executable_relative_path,
        build_jobs=settings.build_jobs,
        timeout_sec=settings.build_timeout_sec,
    )
    deployments = create_deployment_services(graph.deployments, repositories, settings)
    contract_reader = RuntimeContractReader()
    telemetry_reader = McapRunReader()
    if settings.execution_mode == "embedded":
        build_scheduler: BuildDispatcher = InProcessBuildScheduler(
            builds, settings.max_concurrent_builds
        )
        runner = runner_override or ExternalSimulationRunner(
            settings.runner_path, settings.run_timeout_sec
        )
        execution = RunExecutionService(
            graph.runs,
            runner,
            RunTelemetryProcessor(telemetry_reader),
            settings.data_dir,
            builds,
            graph.instances,
            repositories,
            contract_reader,
        )
        run_scheduler: RunDispatcher = InProcessRunScheduler(
            execution,
            settings.max_concurrent_runs,
            build_scheduler,
        )
    else:
        durable_dispatcher = DurableJobDispatcher()
        build_scheduler = durable_dispatcher
        run_scheduler = durable_dispatcher
    return ExecutionGraph(
        repositories,
        builds,
        deployments,
        contract_reader,
        telemetry_reader,
        build_scheduler,
        run_scheduler,
    )


def create_deployment_services(
    records: DeploymentRepository,
    repositories: RepositoryManager,
    settings: Settings,
) -> DeploymentManager:
    return DeploymentManager(records, repositories, settings)


def create_scenario_services(
    settings: Settings,
    catalog: ScenarioCatalogRepository,
    repositories: RepositoryManager,
) -> ScenarioGraph:
    validator = ScenarioValidator()
    compatibility = StableScenarioContractResolver(repositories)
    sftp_source, sftp_error = _sftp_source(settings)
    sync = ScenarioSyncService(
        source=sftp_source,
        cache_root=settings.remote_scenario_cache_dir,
        catalog=catalog,
        validator=validator,
        compatibility=compatibility,
        configuration_error=sftp_error,
    )
    scenarios = ScenarioService(
        settings.scenario_dir,
        validator,
        remote_cache_dir=settings.remote_scenario_cache_dir,
        managed_scenario_dir=settings.resolved_managed_scenario_dir,
        catalog_repository=catalog,
        sources={
            "bundled": DirectoryScenarioSource(
                settings.scenario_dir, ScenarioSourceType.BUNDLED
            ),
            "managed": DirectoryScenarioSource(
                settings.resolved_managed_scenario_dir,
                ScenarioSourceType.MANAGED,
            ),
            "sftp": CatalogCachedScenarioSource(
                settings.remote_scenario_cache_dir, catalog
            ),
        },
        stable_contract_resolver=compatibility,
    )
    return ScenarioGraph(
        validator=validator,
        compatibility=compatibility,
        sync=sync,
        scenarios=scenarios,
        writer=ScenarioWriteService(
            settings.resolved_managed_scenario_dir, validator, compatibility
        ),
        inspector=ScenarioInspectionService(scenarios, validator, catalog),
    )


async def bootstrap_application(
    app: FastAPI,
    settings: Settings,
    build_info: BuildInfo,
    runner_override: SimulationRunner | None = None,
) -> ApplicationRuntime:
    """Construct and attach the explicit JSB1 application dependency graph."""
    for path in (
        settings.data_dir,
        settings.runs_dir,
        settings.resolved_repository_root,
        settings.resolved_worktree_root,
        settings.resolved_build_root,
        settings.resolved_deployment_root,
        settings.resolved_caddy_fragments_dir,
        settings.resolved_managed_scenario_dir,
    ):
        path.mkdir(parents=True, exist_ok=True)

    persistence = create_repositories(settings)
    database = persistence.database
    runs = persistence.runs
    comparisons = persistence.comparisons
    repository_records = persistence.repositories
    build_records = persistence.builds
    instances = persistence.instances
    deployment_records = persistence.deployments
    scenario_catalog = persistence.scenario_catalog

    recovered_runs = 0
    recovered_builds = 0
    recovered_instances = 0
    if settings.execution_mode == "embedded":
        interrupted_run_ids = runs.ids_with_statuses([RunStatus.RUNNING])
        interrupted_build_ids = build_records.ids_with_statuses([BuildStatus.RUNNING])
        recovered_runs = runs.fail_running_from_previous_worker()
        recovered_builds = build_records.fail_running_from_previous_worker()
        recovered_instances = instances.fail_running_from_previous_worker()
        for run_id in interrupted_run_ids:
            ExecutionPipelineRecorder(runs, run_id, RUN_PIPELINE).fail_current(
                "backend restarted before run completed"
            )
        for build_id in interrupted_build_ids:
            ExecutionPipelineRecorder(
                build_records, build_id, BUILD_PIPELINE
            ).fail_current("backend restarted before build completed")
    recovered_deployments = deployment_records.fail_interrupted()
    if (
        recovered_runs
        or recovered_builds
        or recovered_instances
        or recovered_deployments
    ):
        LOGGER.warning(
            "recovered interrupted runs=%s builds=%s instances=%s deployments=%s",
            recovered_runs,
            recovered_builds,
            recovered_instances,
            recovered_deployments,
        )

    execution_graph = create_execution_services(settings, persistence, runner_override)
    repositories = execution_graph.repositories
    if settings.bootstrap_runtime_repository:
        try:
            runtime_status = await asyncio.to_thread(
                repositories.ensure_runtime_repository,
                settings.jsb0_repository_url,
                settings.resolved_jsb0_repository_path,
                settings.jsb0_default_branch,
            )
            if runtime_status.status != "ready":
                LOGGER.warning(
                    "JSB0 Runtime repository bootstrap incomplete: %s",
                    runtime_status.error,
                )
        except (GitOperationError, InvalidRepositoryPath, RepositoryConflict) as exc:
            repositories.record_runtime_configuration_error(exc)
            LOGGER.error("JSB0 Runtime repository configuration failed: %s", exc)

    builds = execution_graph.builds
    deployments = execution_graph.deployments
    contract_reader = execution_graph.contract_reader
    reader = execution_graph.telemetry_reader
    build_scheduler = execution_graph.build_scheduler
    scheduler = execution_graph.run_scheduler

    scenario_graph = create_scenario_services(settings, scenario_catalog, repositories)
    validator = scenario_graph.validator
    compatibility = scenario_graph.compatibility
    sync = scenario_graph.sync
    scenarios = scenario_graph.scenarios
    variants = RuntimeVariantService(contract_reader)
    controller_parameters = RuntimeControllerParameterService(contract_reader)
    snapshots = ScenarioSnapshotService(settings.data_dir)

    services = {
        "settings": settings,
        "build_info": build_info,
        "database": database,
        "health": HealthService(
            database,
            settings.runner_path,
            execution_mode=settings.execution_mode,
            runtime_repository_path=settings.resolved_jsb0_repository_path,
        ),
        "repository": runs,
        "comparisons": comparisons,
        "jsb_repositories": repository_records,
        "repository_manager": repositories,
        "builds": build_records,
        "instances": instances,
        "deployments": deployment_records,
        "scenario_catalog": scenario_catalog,
        "deployment_manager": deployments,
        "build_manager": builds,
        "build_requests": BuildRequestService(builds, build_scheduler),
        "build_scheduler": build_scheduler,
        "reader": reader,
        "scenario_validator": validator,
        "scenario_compatibility": compatibility,
        "scenario_sync": sync,
        "scenario_writer": scenario_graph.writer,
        "scenarios": scenarios,
        "scenario_inspector": scenario_graph.inspector,
        "runtime_variants": variants,
        "runtime_contract_reader": contract_reader,
        "controller_parameters": controller_parameters,
        "scenario_snapshots": snapshots,
        "artifacts": ArtifactService(settings.data_dir),
        "scheduler": scheduler,
    }
    services["telemetry_queries"] = TelemetryQueryService(
        runs,
        reader,
        services["artifacts"],
        repositories,
        contract_reader,
    )
    services["run_analysis"] = RunAnalysisService(
        runs,
        services["artifacts"],
        RollHoldAnalyzer(reader),
        repositories,
        contract_reader,
    )
    services["run_deletion"] = RunDeletionService(runs, settings.runs_dir)
    services["run_queries"] = RunQueryService(
        runs, instances, scenarios, services["artifacts"]
    )
    services["comparison_queries"] = ComparisonQueryService(comparisons)
    services["scenario_snapshot_queries"] = ScenarioSnapshotQueryService(
        runs, comparisons, services["artifacts"]
    )
    services["run_creation"] = RunCreationService(
        runs,
        scenarios,
        repositories,
        builds,
        variants,
        controller_parameters,
        snapshots,
        instances,
        build_scheduler,
        scheduler,
        contract_reader,
    )
    services["comparison_creation"] = ComparisonCreationService(
        comparisons,
        runs,
        scenarios,
        repositories,
        builds,
        variants,
        snapshots,
        instances,
        build_scheduler,
        scheduler,
        contract_reader,
    )
    for name, service in services.items():
        setattr(app.state, name, service)
    if settings.execution_mode == "embedded":
        for build_id in build_records.list_ids(BuildStatus.QUEUED):
            build_scheduler.submit(build_id)
        for run_id, build_status in runs.queued_candidates():
            run = runs.get(run_id)
            scheduler.submit(
                run_id,
                wait_for_build_id=(
                    run.build_id if build_status == BuildStatus.QUEUED.value else None
                ),
            )
    return ApplicationRuntime(scheduler, build_scheduler, deployments)


def _sftp_source(
    settings: Settings,
) -> tuple[SftpScenarioSource | None, str | None]:
    if not settings.scenario_sftp_host:
        return None, None
    if not settings.scenario_sftp_user:
        message = "SFTP scenario source configuration is incomplete: user is missing"
        LOGGER.warning(
            "SFTP scenario source disabled: JSB1_SCENARIO_SFTP_USER is missing"
        )
        return None, message
    try:
        return (
            SftpScenarioSource(
                host=settings.scenario_sftp_host,
                port=settings.scenario_sftp_port,
                username=settings.scenario_sftp_user,
                root=settings.scenario_sftp_root,
                key_path=(
                    settings.scenario_sftp_key_path.expanduser().resolve()
                    if settings.scenario_sftp_key_path is not None
                    else None
                ),
                password=settings.scenario_sftp_password,
                known_hosts_path=(
                    settings.scenario_sftp_known_hosts_path.expanduser().resolve()
                    if settings.scenario_sftp_known_hosts_path is not None
                    else None
                ),
                timeout_sec=settings.scenario_sftp_timeout_sec,
            ),
            None,
        )
    except ValueError as exc:
        message = f"Invalid SFTP scenario configuration: {exc}"
        LOGGER.warning(message)
        return None, message
