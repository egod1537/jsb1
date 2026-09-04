from __future__ import annotations

import logging
from typing import Protocol

logger = logging.getLogger(__name__)


class BuildDispatcher(Protocol):
    def submit(self, build_id: int) -> None: ...

    async def shutdown(self) -> None: ...


class RunDispatcher(Protocol):
    def submit(self, run_id: int, *, wait_for_build_id: int | None = None) -> None: ...

    async def shutdown(self) -> None: ...


class DurableJobDispatcher:
    """Wake-up boundary for the database-backed external worker queue.

    Creating a queued Build or Run is itself the durable enqueue operation.
    The worker polls those rows, so the API process intentionally owns no
    execution task and can restart independently.
    """

    def submit(self, job_id: int, *, wait_for_build_id: int | None = None) -> None:
        logger.info(
            "durable job queued id=%s wait_for_build_id=%s",
            job_id,
            wait_for_build_id,
        )

    async def shutdown(self) -> None:
        return None
