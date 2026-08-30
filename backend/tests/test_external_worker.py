from __future__ import annotations

import asyncio

from fastapi.testclient import TestClient

from app.main import create_app
from app.domain.build import BuildStatus
from app.repositories.database import Database
from app.repositories.runs import RunRepository
from app.worker import create_worker
from app.workers.execution_worker import ExecutionWorker
from tests.conftest import FakeSimulationRunner


class RecordingScheduler:
    def __init__(self) -> None:
        self.submitted: list[int] = []

    def submit(self, job_id: int, **_kwargs) -> None:
        self.submitted.append(job_id)

    def is_scheduled(self, job_id: int) -> bool:
        return job_id in self.submitted

    async def shutdown(self) -> None:
        return None


class QueueBuilds:
    def list_ids(self, status: BuildStatus) -> list[int]:
        assert status is BuildStatus.QUEUED
        return [7]


class QueueRuns:
    build_status = BuildStatus.QUEUED.value

    def queued_candidates(self) -> list[tuple[int, str | None]]:
        return [(11, self.build_status)]


def test_api_restart_preserves_durable_queue_and_worker_executes(settings) -> None:
    external = settings.model_copy(update={"execution_mode": "external"})

    with TestClient(create_app(external)) as client:
        response = client.post(
            "/api/runs",
            json={"scenario": "roll_hold_5deg.yaml", "commit_sha": "abc123"},
        )
        assert response.status_code == 202
        run_id = response.json()["id"]
        assert client.get(f"/api/runs/{run_id}").json()["run"]["status"] == "queued"

    # Starting a fresh API process must not consume or fail durable queue rows.
    with TestClient(create_app(external)) as restarted:
        assert restarted.get(f"/api/runs/{run_id}").json()["run"]["status"] == "queued"

    worker = create_worker(external, FakeSimulationRunner())

    async def execute_queued_run() -> None:
        worker.tick()
        await worker.run_scheduler.wait(run_id)
        await worker.shutdown()

    asyncio.run(execute_queued_run())

    migrations = external.data_dir.parent / "unused"
    database = Database(external.resolved_database_path, migrations)
    assert RunRepository(database).get(run_id).status.value == "completed"


def test_worker_waits_for_build_before_dispatching_run() -> None:
    builds = QueueBuilds()
    runs = QueueRuns()
    build_scheduler = RecordingScheduler()
    run_scheduler = RecordingScheduler()
    worker = ExecutionWorker(
        builds,  # type: ignore[arg-type]
        runs,  # type: ignore[arg-type]
        build_scheduler,  # type: ignore[arg-type]
        run_scheduler,  # type: ignore[arg-type]
        poll_interval_sec=0.01,
    )

    worker.tick()
    assert build_scheduler.submitted == [7]
    assert run_scheduler.submitted == []

    runs.build_status = BuildStatus.COMPLETED.value
    worker.tick()
    assert run_scheduler.submitted == [11]
