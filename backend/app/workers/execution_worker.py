from __future__ import annotations

import asyncio
import logging

from app.domain.build import BuildStatus
from app.repositories.builds import BuildRepository
from app.repositories.runs import RunRepository
from app.workers.build_scheduler import InProcessBuildScheduler
from app.workers.scheduler import InProcessRunScheduler

logger = logging.getLogger(__name__)


class ExecutionWorker:
    """Poll durable rows and dispatch IDs; services atomically claim the work."""

    def __init__(
        self,
        builds: BuildRepository,
        runs: RunRepository,
        build_scheduler: InProcessBuildScheduler,
        run_scheduler: InProcessRunScheduler,
        *,
        poll_interval_sec: float,
    ) -> None:
        self.builds = builds
        self.runs = runs
        self.build_scheduler = build_scheduler
        self.run_scheduler = run_scheduler
        self.poll_interval_sec = poll_interval_sec

    async def run(self, stop: asyncio.Event | None = None) -> None:
        stop_event = stop or asyncio.Event()
        logger.info("execution worker started")
        try:
            while not stop_event.is_set():
                self.tick()
                try:
                    await asyncio.wait_for(
                        stop_event.wait(), timeout=self.poll_interval_sec
                    )
                except TimeoutError:
                    pass
        finally:
            await self.shutdown()
            logger.info("execution worker stopped")

    def tick(self) -> None:
        for build_id in self.builds.list_ids(BuildStatus.QUEUED):
            if not self.build_scheduler.is_scheduled(build_id):
                self.build_scheduler.submit(build_id)

        ready_build_states = {
            None,
            BuildStatus.COMPLETED.value,
            BuildStatus.FAILED.value,
        }
        for run_id, build_status in self.runs.queued_candidates():
            if (
                build_status in ready_build_states
                and not self.run_scheduler.is_scheduled(run_id)
            ):
                self.run_scheduler.submit(run_id)

    async def shutdown(self) -> None:
        # Stop Runs before Builds so no new execution can start while the
        # underlying immutable binary is being cancelled.
        await self.run_scheduler.shutdown()
        await self.build_scheduler.shutdown()
