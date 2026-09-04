from __future__ import annotations

import asyncio
import logging
import signal

from app.config.settings import get_settings
from app.infrastructure.execution import WorkerProcessLock
from app.workers.composition import create_worker

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s %(message)s",
)
logger = logging.getLogger(__name__)


async def _run() -> None:
    settings = get_settings()
    process_lock = WorkerProcessLock(settings.data_dir / "execution-worker.lock")
    process_lock.acquire()
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for signum in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(signum, stop.set)
    try:
        await create_worker(settings).run(stop)
    finally:
        process_lock.release()


def main() -> None:
    asyncio.run(_run())


if __name__ == "__main__":
    main()
