from __future__ import annotations

import asyncio
import fcntl
import logging
import signal
from pathlib import Path

from app.analysis.mcap_reader import McapRunReader
from app.config.settings import Settings, get_settings
from app.domain.build import BuildStatus
from app.domain.models import RunStatus
from app.repositories.builds import BuildRepository
from app.repositories.database import Database
from app.repositories.instances import InstanceRepository
from app.repositories.jsb_repository_repository import JsbRepositoryRepository
from app.repositories.runs import RunRepository
from app.services.build_manager import BuildManager
from app.services.execution import RunExecutionService
from app.services.execution_pipeline import (
    BUILD_PIPELINE,
    RUN_PIPELINE,
    ExecutionPipelineRecorder,
)
from app.services.repository_manager import RepositoryManager
from app.services.runner import ExternalSimulationRunner, SimulationRunner
from app.services.telemetry_processing import RunTelemetryProcessor
from app.workers.build_scheduler import InProcessBuildScheduler
from app.workers.execution_worker import ExecutionWorker
from app.workers.scheduler import InProcessRunScheduler


logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s %(message)s",
)
logger = logging.getLogger(__name__)


def create_worker(
    settings: Settings | None = None,
    runner_override: SimulationRunner | None = None,
) -> ExecutionWorker:
    worker_settings = settings or get_settings()
    for directory in (
        worker_settings.data_dir,
        worker_settings.runs_dir,
        worker_settings.resolved_repository_root,
        worker_settings.resolved_worktree_root,
        worker_settings.resolved_build_root,
    ):
        directory.mkdir(parents=True, exist_ok=True)

    migrations = Path(__file__).resolve().parents[1] / "migrations"
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
    )
    build_scheduler = InProcessBuildScheduler(
        builds, worker_settings.max_concurrent_builds
    )
    run_scheduler = InProcessRunScheduler(
        execution, worker_settings.max_concurrent_runs
    )

    interrupted_build_ids = build_records.ids_with_statuses([BuildStatus.RUNNING])
    interrupted_run_ids = runs.ids_with_statuses([RunStatus.RUNNING])
    recovered_builds = build_records.fail_running_from_previous_worker()
    recovered_runs = runs.fail_running_from_previous_worker()
    recovered_instances = instances.fail_running_from_previous_worker()
    for build_id in interrupted_build_ids:
        ExecutionPipelineRecorder(
            build_records, build_id, BUILD_PIPELINE
        ).fail_current("execution worker restarted during build")
    for run_id in interrupted_run_ids:
        ExecutionPipelineRecorder(runs, run_id, RUN_PIPELINE).fail_current(
            "execution worker restarted during run"
        )
    if recovered_builds or recovered_runs or recovered_instances:
        logger.warning(
            "recovered interrupted worker state builds=%s runs=%s instances=%s",
            recovered_builds,
            recovered_runs,
            recovered_instances,
        )

    return ExecutionWorker(
        build_records,
        runs,
        build_scheduler,
        run_scheduler,
        poll_interval_sec=worker_settings.worker_poll_interval_sec,
    )


async def _run() -> None:
    settings = get_settings()
    lock_path = settings.data_dir / "execution-worker.lock"
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    lock_file = lock_path.open("a+", encoding="utf-8")
    try:
        fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError as exc:
        lock_file.close()
        raise RuntimeError(
            "another execution worker already owns this data directory"
        ) from exc
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for signum in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(signum, stop.set)
    try:
        await create_worker(settings).run(stop)
    finally:
        fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)
        lock_file.close()


def main() -> None:
    asyncio.run(_run())


if __name__ == "__main__":
    main()
