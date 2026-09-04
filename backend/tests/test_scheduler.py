from __future__ import annotations

import asyncio

import pytest

from app.domain.models import ComparisonRun, ComparisonStatus, RunStatus
from app.repositories.comparisons import ComparisonRepository
from app.workers.scheduler import InProcessRunScheduler


class RecordingExecution:
    def __init__(self) -> None:
        self.active = 0
        self.max_active = 0

    async def execute(self, run_id: int) -> None:
        self.active += 1
        self.max_active = max(self.max_active, self.active)
        await asyncio.sleep(0.02)
        self.active -= 1


class BlockingExecution:
    def __init__(self) -> None:
        self.started = asyncio.Event()
        self.cancelled = asyncio.Event()

    async def execute(self, run_id: int) -> None:
        assert run_id == 101
        self.started.set()
        try:
            await asyncio.Event().wait()
        except asyncio.CancelledError:
            self.cancelled.set()
            raise


@pytest.mark.asyncio
@pytest.mark.parametrize(("limit", "expected_parallelism"), [(1, 1), (2, 2)])
async def test_child_runs_respect_scheduler_concurrency(
    limit: int, expected_parallelism: int
) -> None:
    execution = RecordingExecution()
    scheduler = InProcessRunScheduler(execution, limit)
    scheduler.submit(101)
    scheduler.submit(102)
    await asyncio.gather(scheduler.wait(101), scheduler.wait(102))
    assert execution.max_active == expected_parallelism
    await scheduler.shutdown()


@pytest.mark.asyncio
async def test_scheduler_graceful_shutdown_cancels_and_joins_active_work() -> None:
    execution = BlockingExecution()
    scheduler = InProcessRunScheduler(execution, 1)
    scheduler.submit(101)
    await execution.started.wait()

    await scheduler.shutdown()

    assert execution.cancelled.is_set()


def test_comparison_status_aggregation() -> None:
    def child(run_id: int, status: RunStatus) -> ComparisonRun:
        return ComparisonRun(
            run_id=run_id, execution_variant=f"variant-{run_id}", status=status
        )

    assert (
        ComparisonRepository.aggregate_status(
            [child(1, RunStatus.COMPLETED), child(2, RunStatus.COMPLETED)]
        )
        is ComparisonStatus.COMPLETED
    )
    assert (
        ComparisonRepository.aggregate_status(
            [child(1, RunStatus.FAILED), child(2, RunStatus.FAILED)]
        )
        is ComparisonStatus.FAILED
    )
    assert (
        ComparisonRepository.aggregate_status(
            [child(1, RunStatus.COMPLETED), child(2, RunStatus.FAILED)]
        )
        is ComparisonStatus.PARTIAL_FAILED
    )
    assert (
        ComparisonRepository.aggregate_status(
            [child(1, RunStatus.RUNNING), child(2, RunStatus.QUEUED)]
        )
        is ComparisonStatus.RUNNING
    )
