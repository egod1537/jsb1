from __future__ import annotations

import asyncio
import logging

from app.services.build_manager import BuildManager


logger = logging.getLogger(__name__)


class InProcessBuildScheduler:
    def __init__(self, manager: BuildManager, max_concurrent_builds: int) -> None:
        self.manager = manager
        self._semaphore = asyncio.Semaphore(max_concurrent_builds)
        self._tasks: dict[int, asyncio.Task[None]] = {}

    def submit(self, build_id: int) -> None:
        if build_id in self._tasks:
            raise ValueError(f"build {build_id} is already scheduled")
        task = asyncio.create_task(self._run(build_id), name=f"jsb1-build-{build_id}")
        self._tasks[build_id] = task
        task.add_done_callback(lambda _task: self._tasks.pop(build_id, None))
        logger.info("build submitted id=%s", build_id)

    async def _run(self, build_id: int) -> None:
        async with self._semaphore:
            await self.manager.execute(build_id)

    async def wait(self, build_id: int) -> None:
        task = self._tasks.get(build_id)
        if task is not None:
            await task

    async def shutdown(self) -> None:
        tasks = list(self._tasks.values())
        for task in tasks:
            task.cancel()
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)
