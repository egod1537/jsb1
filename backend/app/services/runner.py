from __future__ import annotations

import asyncio
import logging
import os
import shutil
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol


logger = logging.getLogger(__name__)


class RunnerUnavailable(RuntimeError):
    pass


class RunnerTimedOut(RuntimeError):
    pass


@dataclass(frozen=True)
class RunnerResult:
    exit_code: int
    wall_time_sec: float


class SimulationRunner(Protocol):
    async def run(
        self,
        *,
        scenario_path: Path,
        output_directory: Path,
        autopilot: str,
        log_path: Path,
    ) -> RunnerResult: ...


class ExternalSimulationRunner:
    def __init__(self, executable: Path, timeout_sec: float) -> None:
        self.executable = executable
        self.timeout_sec = timeout_sec

    def available(self) -> bool:
        value = str(self.executable)
        return self.executable.is_file() or shutil.which(value) is not None

    async def run(
        self,
        *,
        scenario_path: Path,
        output_directory: Path,
        autopilot: str,
        log_path: Path,
    ) -> RunnerResult:
        if not self.available():
            raise RunnerUnavailable(f"runner executable not found: {self.executable}")
        command = [
            str(self.executable),
            "--scenario",
            str(scenario_path),
            "--output",
            str(output_directory),
            "--autopilot",
            autopilot,
        ]
        logger.info("starting simulation command=%r", command)
        started = time.perf_counter()
        log_path.parent.mkdir(parents=True, exist_ok=True)
        with log_path.open("wb") as log_file:
            process = await asyncio.create_subprocess_exec(
                *command,
                stdout=log_file,
                stderr=asyncio.subprocess.STDOUT,
                env=os.environ.copy(),
            )
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
        logger.info("simulation exited code=%s wall_time=%.3f", process.returncode, elapsed)
        return RunnerResult(exit_code=int(process.returncode), wall_time_sec=elapsed)

