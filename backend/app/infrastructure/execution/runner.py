from __future__ import annotations

import asyncio
import logging
import os
import shutil
import time
from collections.abc import Callable, Sequence
from pathlib import Path

from app.domain.errors import ExternalProcessTimedOut, ExternalProcessUnavailable
from app.domain.execution import RunnerResult

logger = logging.getLogger(__name__)


class RunnerUnavailable(ExternalProcessUnavailable):
    pass


class RunnerTimedOut(ExternalProcessTimedOut):
    pass


class ExternalSimulationRunner:
    """Own executable discovery and subprocess lifecycle for JSB0 runs."""

    def __init__(self, executable: Path, timeout_sec: float) -> None:
        self.executable = executable
        self.timeout_sec = timeout_sec

    def available(self, executable: Path | None = None) -> bool:
        selected = executable or self.executable
        value = str(selected)
        return selected.is_file() or shutil.which(value) is not None

    async def run(
        self,
        *,
        argv: Sequence[str],
        stdout_path: Path,
        stderr_path: Path,
        executable_path: Path | None = None,
        cwd: Path | None = None,
        on_started: Callable[[int], None] | None = None,
    ) -> RunnerResult:
        executable = executable_path or self.executable
        if not self.available(executable):
            raise RunnerUnavailable(f"runner executable not found: {executable}")
        command = [str(executable), *argv]
        logger.info("starting simulation command=%r", command)
        started = time.perf_counter()
        stdout_path.parent.mkdir(parents=True, exist_ok=True)
        stderr_path.parent.mkdir(parents=True, exist_ok=True)
        with stdout_path.open("wb") as stdout_file, stderr_path.open("wb") as stderr_file:
            process = await asyncio.create_subprocess_exec(
                *command,
                stdout=stdout_file,
                stderr=stderr_file,
                env=os.environ.copy(),
                cwd=cwd,
            )
            if on_started is not None:
                try:
                    on_started(process.pid)
                except Exception:
                    process.terminate()
                    await process.wait()
                    raise
            try:
                await asyncio.wait_for(process.wait(), timeout=self.timeout_sec)
            except TimeoutError as exc:
                process.terminate()
                try:
                    await asyncio.wait_for(process.wait(), timeout=5)
                except TimeoutError:
                    process.kill()
                    await process.wait()
                raise RunnerTimedOut(
                    f"simulation exceeded timeout of {self.timeout_sec:g} seconds"
                ) from exc
            except asyncio.CancelledError:
                process.terminate()
                await process.wait()
                raise
        elapsed = time.perf_counter() - started
        logger.info(
            "simulation exited code=%s wall_time=%.3f", process.returncode, elapsed
        )
        return RunnerResult(exit_code=int(process.returncode), wall_time_sec=elapsed)
