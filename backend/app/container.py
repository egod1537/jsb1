from __future__ import annotations

import asyncio
import logging
from dataclasses import dataclass
from pathlib import Path

from fastapi import FastAPI

from app.analysis.mcap_reader import McapRunReader
from app.analysis.roll_hold_analyzer import RollHoldAnalyzer
from app.config.settings import Settings
from app.domain.build_info import BuildInfo
from app.domain.build import BuildStatus
from app.domain.models import RunStatus
from app.repositories.builds import BuildRepository
from app.repositories.comparisons import ComparisonRepository
from app.repositories.database import Database
from app.repositories.deployments import DeploymentRepository
from app.repositories.instances import InstanceRepository
from app.repositories.jsb_repository_repository import (
    JsbRepositoryRepository,
    RepositoryConflict,
)
from app.repositories.runs import RunRepository
from app.repositories.scenario_catalog import ScenarioCatalogRepository
from app.services.artifacts import ArtifactService
from app.services.build_manager import BuildManager
from app.services.comparison_creation import ComparisonCreationService
from app.services.controller_parameters import RuntimeControllerParameterService
from app.services.deployment_manager import DeploymentManager
from app.services.execution import RunExecutionService
from app.services.execution_pipeline import (
    BUILD_PIPELINE,
    RUN_PIPELINE,
    ExecutionPipelineRecorder,
)
from app.services.repository_manager import (
    GitOperationError,
    InvalidRepositoryPath,
    RepositoryManager,
)
from app.services.runner import ExternalSimulationRunner, SimulationRunner
from app.services.run_creation import RunCreationService
from app.services.run_analysis import RunAnalysisService
from app.services.run_deletion import RunDeletionService
from app.services.runtime_variants import RuntimeVariantService
from app.services.scenario_inspection import ScenarioInspectionService
from app.services.scenario_snapshots import ScenarioSnapshotService
from app.services.scenario_sources.sftp import SftpScenarioSource
from app.services.scenario_sync import (
    ScenarioCompatibilityResolver,
    ScenarioSyncService,
)
from app.services.scenario_validator import ScenarioValidator
from app.services.scenario_writes import ScenarioWriteService
from app.services.scenarios import ScenarioService
from app.services.telemetry_processing import RunTelemetryProcessor
from app.services.telemetry_queries import TelemetryQueryService
from app.workers.build_scheduler import InProcessBuildScheduler
from app.workers.dispatch import BuildDispatcher, DurableJobDispatcher, RunDispatcher
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

    migrations = Path(__file__).resolve().parents[1] / "migrations"
    database = Database(settings.resolved_database_path, migrations)
    database.initialize()
    runs = RunRepository(database)
    comparisons = ComparisonRepository(database)
    repository_records = JsbRepositoryRepository(database)
    build_records = BuildRepository(database)
    instances = InstanceRepository(database)
    deployment_records = DeploymentRepository(database)
    scenario_catalog = ScenarioCatalogRepository(database)

    recovered_runs = 0
    recovered_builds = 0
    recovered_instances = 0
    if settings.execution_mode == "embedded":
        interrupted_run_ids = runs.ids_with_statuses(
            [RunStatus.QUEUED, RunStatus.RUNNING]
        )
        interrupted_build_ids = build_records.ids_with_statuses(
            [BuildStatus.QUEUED, BuildStatus.RUNNING]
        )
        recovered_runs = runs.fail_incomplete_from_previous_process()
        recovered_builds = build_records.fail_incomplete_from_previous_process()
        recovered_instances = instances.fail_incomplete_from_previous_process()
        for run_id in interrupted_run_ids:
            ExecutionPipelineRecorder(runs, run_id, RUN_PIPELINE).fail_current(
                "backend restarted before run completed"
            )
        for build_id in interrupted_build_ids:
            ExecutionPipelineRecorder(
                build_records, build_id, BUILD_PIPELINE
            ).fail_current("backend restarted before build completed")
    recovered_deployments = deployment_records.fail_interrupted()
    if recovered_runs or recovered_builds or recovered_instances or recovered_deployments:
        LOGGER.warning(
            "recovered interrupted runs=%s builds=%s instances=%s deployments=%s",
            recovered_runs,
            recovered_builds,
            recovered_instances,
            recovered_deployments,
        )

    repositories = RepositoryManager(
        repository_records,
        settings.resolved_repository_root,
        settings.resolved_worktree_root,
    )
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

    builds = BuildManager(
        build_records,
        repositories,
        settings.resolved_build_root,
        executable_relative_path=settings.build_executable_relative_path,
        build_jobs=settings.build_jobs,
        timeout_sec=settings.build_timeout_sec,
    )
    deployments = DeploymentManager(deployment_records, repositories, settings)
    reader = McapRunReader()
    if settings.execution_mode == "embedded":
        build_scheduler: BuildDispatcher = InProcessBuildScheduler(
            builds, settings.max_concurrent_builds
        )
        runner = runner_override or ExternalSimulationRunner(
            settings.runner_path, settings.run_timeout_sec
        )
        execution = RunExecutionService(
            runs,
            runner,
            RunTelemetryProcessor(reader),
            settings.data_dir,
            builds,
            instances,
        )
        scheduler: RunDispatcher = InProcessRunScheduler(
            execution,
            settings.max_concurrent_runs,
            build_scheduler,
        )
    else:
        durable_dispatcher = DurableJobDispatcher()
        build_scheduler = durable_dispatcher
        scheduler = durable_dispatcher

    validator = ScenarioValidator()
    compatibility = ScenarioCompatibilityResolver(repositories)
    sftp_source, sftp_error = _sftp_source(settings)
    sync = ScenarioSyncService(
        source=sftp_source,
        cache_root=settings.remote_scenario_cache_dir,
        catalog=scenario_catalog,
        validator=validator,
        compatibility=compatibility,
        configuration_error=sftp_error,
    )
    scenarios = ScenarioService(
        settings.scenario_dir,
        validator,
        remote_cache_dir=settings.remote_scenario_cache_dir,
        managed_scenario_dir=settings.resolved_managed_scenario_dir,
        catalog_repository=scenario_catalog,
    )
    variants = RuntimeVariantService()
    controller_parameters = RuntimeControllerParameterService()
    snapshots = ScenarioSnapshotService(settings.data_dir)

    services = {
        "settings": settings,
        "build_info": build_info,
        "database": database,
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
        "build_scheduler": build_scheduler,
        "reader": reader,
        "scenario_validator": validator,
        "scenario_compatibility": compatibility,
        "scenario_sync": sync,
        "scenario_writer": ScenarioWriteService(
            settings.resolved_managed_scenario_dir, validator, compatibility
        ),
        "scenarios": scenarios,
        "scenario_inspector": ScenarioInspectionService(
            scenarios, validator, scenario_catalog
        ),
        "runtime_variants": variants,
        "controller_parameters": controller_parameters,
        "scenario_snapshots": snapshots,
        "artifacts": ArtifactService(settings.data_dir),
        "scheduler": scheduler,
    }
    services["telemetry_queries"] = TelemetryQueryService(
        runs, reader, services["artifacts"]
    )
    services["run_analysis"] = RunAnalysisService(
        runs, services["artifacts"], RollHoldAnalyzer(reader)
    )
    services["run_deletion"] = RunDeletionService(runs, settings.runs_dir)
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
    )
    for name, service in services.items():
        setattr(app.state, name, service)
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
