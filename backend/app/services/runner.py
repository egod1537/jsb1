from __future__ import annotations

import asyncio
import logging
import os
import shutil
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Protocol


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
        log_path: Path,
        executable_path: Path | None = None,
        parameters_path: Path | None = None,
        on_started: Callable[[int], None] | None = None,
    ) -> RunnerResult: ...


class ExternalSimulationRunner:
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
        scenario_path: Path,
        output_directory: Path,
        log_path: Path,
        executable_path: Path | None = None,
        parameters_path: Path | None = None,
        on_started: Callable[[int], None] | None = None,
    ) -> RunnerResult:
        executable = executable_path or self.executable
        if not self.available(executable):
            raise RunnerUnavailable(f"runner executable not found: {executable}")
        command = [
            str(executable),
            "--scenario",
            str(scenario_path),
            "--output",
            str(output_directory),
        ]
        if parameters_path is not None:
            if not parameters_path.is_file():
                raise RunnerUnavailable(
                    f"controller parameter file not found: {parameters_path}"
                )
            expected_path = output_directory / "parameters.yaml"
            if parameters_path.resolve() != expected_path.resolve():
                raise RunnerUnavailable(
                    "controller parameter file must be output/parameters.yaml"
                )
        logger.info("starting simulation command=%r", command)
        started = time.perf_counter()
        log_path.parent.mkdir(parents=True, exist_ok=True)
        with log_path.open("wb") as log_file:
            process = await asyncio.create_subprocess_exec(
                *command,
                stdout=log_file,
                stderr=asyncio.subprocess.STDOUT,
                env=os.environ.copy(),
                cwd=executable.resolve().parent if executable_path is not None else None,
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
        logger.info("simulation exited code=%s wall_time=%.3f", process.returncode, elapsed)
        return RunnerResult(exit_code=int(process.returncode), wall_time_sec=elapsed)
