from __future__ import annotations

import asyncio
import logging
import os
from pathlib import Path


logger = logging.getLogger(__name__)


class CmakeBuildAdapter:
    """Own CMake command formatting and subprocess lifecycle."""

    def __init__(self, *, jobs: int, timeout_sec: float) -> None:
        self.jobs = jobs
        self.timeout_sec = timeout_sec

    async def build(
        self,
        source: Path,
        build_directory: Path,
        stdout_path: Path,
        stderr_path: Path,
    ) -> None:
        await self.configure(source, build_directory, stdout_path, stderr_path)
        await self.compile(build_directory, stdout_path, stderr_path)

    async def configure(
        self,
        source: Path,
        build_directory: Path,
        stdout_path: Path,
        stderr_path: Path,
    ) -> None:
        await self._command(
            [
                "cmake",
                "-S",
                str(source),
                "-B",
                str(build_directory),
                "-DJSB_BUILD_EDITOR=OFF",
                "-DBUILD_DOCS=OFF",
            ],
            stdout_path,
            stderr_path,
        )

    async def compile(
        self,
        build_directory: Path,
        stdout_path: Path,
        stderr_path: Path,
    ) -> None:
        await self._command(
            [
                "cmake",
                "--build",
                str(build_directory),
                "-j",
                str(self.jobs),
                "--target",
                "jsb-sim-runner",
            ],
            stdout_path,
            stderr_path,
        )

    async def _command(
        self, command: list[str], stdout_path: Path, stderr_path: Path
    ) -> None:
        logger.info("starting build command=%r", command)
        with stdout_path.open("ab") as stdout, stderr_path.open("ab") as stderr:
            process = await asyncio.create_subprocess_exec(
                *command, stdout=stdout, stderr=stderr, env=os.environ.copy()
            )
            try:
                await asyncio.wait_for(process.wait(), timeout=self.timeout_sec)
            except TimeoutError as exc:
                await self._terminate(process)
                raise TimeoutError(
                    f"build command exceeded timeout of {self.timeout_sec:g} seconds"
                ) from exc
            except asyncio.CancelledError:
                await self._terminate(process)
                raise
        if process.returncode != 0:
            raise RuntimeError(f"build command exited with code {process.returncode}")

    @staticmethod
    async def _terminate(process: asyncio.subprocess.Process) -> None:
        process.terminate()
        try:
            await asyncio.wait_for(process.wait(), timeout=5)
        except TimeoutError:
            process.kill()
            await process.wait()
