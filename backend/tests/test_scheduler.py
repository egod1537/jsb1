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


def test_comparison_status_aggregation() -> None:
    def child(run_id: int, status: RunStatus) -> ComparisonRun:
        return ComparisonRun(
            run_id=run_id, execution_variant=f"variant-{run_id}", status=status
        )

    assert ComparisonRepository.aggregate_status([
        child(1, RunStatus.COMPLETED), child(2, RunStatus.COMPLETED)
    ]) is ComparisonStatus.COMPLETED
    assert ComparisonRepository.aggregate_status([
        child(1, RunStatus.FAILED), child(2, RunStatus.FAILED)
    ]) is ComparisonStatus.FAILED
    assert ComparisonRepository.aggregate_status([
        child(1, RunStatus.COMPLETED), child(2, RunStatus.FAILED)
    ]) is ComparisonStatus.PARTIAL_FAILED
    assert ComparisonRepository.aggregate_status([
        child(1, RunStatus.RUNNING), child(2, RunStatus.QUEUED)
    ]) is ComparisonStatus.RUNNING
