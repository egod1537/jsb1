from __future__ import annotations

from pathlib import Path

from app.analysis.mcap_reader import McapRunReader
from app.config.settings import Settings, get_settings
from app.infrastructure.execution import ExternalSimulationRunner
from app.repositories.builds import BuildRepository
from app.repositories.database import Database
from app.repositories.instances import InstanceRepository
from app.repositories.jsb_repository_repository import JsbRepositoryRepository
from app.repositories.runs import RunRepository
from app.services.build_manager import BuildManager
from app.services.execution import RunExecutionService
from app.services.ports import SimulationRunner
from app.services.repository_manager import RepositoryManager
from app.services.runtime_contract import RuntimeContractReader
from app.services.telemetry_processing import RunTelemetryProcessor
from app.workers.build_scheduler import InProcessBuildScheduler
from app.workers.execution_worker import ExecutionWorker
from app.workers.recovery import WorkerRecoveryService
from app.workers.scheduler import InProcessRunScheduler


def create_worker(
    settings: Settings | None = None,
    runner_override: SimulationRunner | None = None,
) -> ExecutionWorker:
    """Build only the dependency graph required by the external worker."""
    worker_settings = settings or get_settings()
    for directory in (
        worker_settings.data_dir,
        worker_settings.runs_dir,
        worker_settings.resolved_repository_root,
        worker_settings.resolved_worktree_root,
        worker_settings.resolved_build_root,
    ):
        directory.mkdir(parents=True, exist_ok=True)

    migrations = Path(__file__).resolve().parents[2] / "migrations"
    database = Database(worker_settings.resolved_database_path, migrations)
    database.initialize()
    runs = RunRepository(database)
    build_records = BuildRepository(database)
    instances = InstanceRepository(database)
    repositories = RepositoryManager(
        JsbRepositoryRepository(database),
        worker_settings.resolved_repository_root,
        worker_settings.resolved_worktree_root,
    )
    builds = BuildManager(
        build_records,
        repositories,
        worker_settings.resolved_build_root,
        executable_relative_path=worker_settings.build_executable_relative_path,
        build_jobs=worker_settings.build_jobs,
        timeout_sec=worker_settings.build_timeout_sec,
    )
    runner = runner_override or ExternalSimulationRunner(
        worker_settings.runner_path, worker_settings.run_timeout_sec
    )
    execution = RunExecutionService(
        runs,
        runner,
        RunTelemetryProcessor(McapRunReader()),
        worker_settings.data_dir,
        builds,
        instances,
        repositories,
        RuntimeContractReader(),
    )
    build_scheduler = InProcessBuildScheduler(
        builds, worker_settings.max_concurrent_builds
    )
    run_scheduler = InProcessRunScheduler(
        execution, worker_settings.max_concurrent_runs
    )
    WorkerRecoveryService(build_records, runs, instances).recover()
    return ExecutionWorker(
        build_records,
        runs,
        build_scheduler,
        run_scheduler,
        poll_interval_sec=worker_settings.worker_poll_interval_sec,
    )
