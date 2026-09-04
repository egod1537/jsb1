from __future__ import annotations

from collections.abc import Callable, Sequence
from pathlib import Path
from typing import Protocol

from app.domain.execution import RunnerResult


class SimulationRunner(Protocol):
    """Application port implemented by the external process adapter."""

    async def run(
        self,
        *,
        argv: Sequence[str],
        stdout_path: Path,
        stderr_path: Path,
        executable_path: Path | None = None,
        cwd: Path | None = None,
        on_started: Callable[[int], None] | None = None,
    ) -> RunnerResult: ...


class BuildDispatcher(Protocol):
    def submit(self, build_id: int) -> None: ...

    async def shutdown(self) -> None: ...


class RunDispatcher(Protocol):
    def submit(self, run_id: int, *, wait_for_build_id: int | None = None) -> None: ...

    async def shutdown(self) -> None: ...
