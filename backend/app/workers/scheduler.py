from __future__ import annotations

import asyncio
import logging

from app.services.execution import RunExecutionService
from app.workers.build_scheduler import InProcessBuildScheduler


logger = logging.getLogger(__name__)


class InProcessRunScheduler:
    """Small single-process queue boundary, replaceable by an external queue later."""

    def __init__(
        self,
        execution: RunExecutionService,
        max_concurrent_runs: int,
        build_scheduler: InProcessBuildScheduler | None = None,
    ) -> None:
        self.execution = execution
        self.build_scheduler = build_scheduler
        self._semaphore = asyncio.Semaphore(max_concurrent_runs)
        self._tasks: dict[int, asyncio.Task[None]] = {}

    def submit(self, run_id: int, *, wait_for_build_id: int | None = None) -> None:
        if run_id in self._tasks:
            raise ValueError(f"run {run_id} is already scheduled")
        task = asyncio.create_task(
            self._run(run_id, wait_for_build_id), name=f"jsb1-run-{run_id}"
        )
        self._tasks[run_id] = task
        task.add_done_callback(lambda _task: self._tasks.pop(run_id, None))
        logger.info("run submitted id=%s", run_id)

    async def _run(self, run_id: int, wait_for_build_id: int | None) -> None:
        if wait_for_build_id is not None:
            if self.build_scheduler is None:
                raise RuntimeError("build scheduler is not configured")
            await self.build_scheduler.wait(wait_for_build_id)
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
