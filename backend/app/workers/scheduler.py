from __future__ import annotations

import asyncio
import logging

from app.services.execution import RunExecutionService


logger = logging.getLogger(__name__)


class InProcessRunScheduler:
    """Small single-process queue boundary, replaceable by an external queue later."""

    def __init__(self, execution: RunExecutionService, max_concurrent_runs: int) -> None:
        self.execution = execution
        self._semaphore = asyncio.Semaphore(max_concurrent_runs)
        self._tasks: dict[int, asyncio.Task[None]] = {}

    def submit(self, run_id: int) -> None:
        if run_id in self._tasks:
            raise ValueError(f"run {run_id} is already scheduled")
        task = asyncio.create_task(self._run(run_id), name=f"jsb1-run-{run_id}")
        self._tasks[run_id] = task
        task.add_done_callback(lambda _task: self._tasks.pop(run_id, None))
        logger.info("run submitted id=%s", run_id)

    async def _run(self, run_id: int) -> None:
        async with self._semaphore:
            await self.execution.execute(run_id)

    async def wait(self, run_id: int) -> None:
        task = self._tasks.get(run_id)
        if task is not None:
            await task

    async def shutdown(self) -> None:
        tasks = list(self._tasks.values())
        for task in tasks:
            task.cancel()
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)

