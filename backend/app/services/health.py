from __future__ import annotations

from pathlib import Path
from typing import Literal

from app.infrastructure.execution import ExecutableProbe
from app.infrastructure.git import RepositoryProbe
from app.repositories.database import Database


class HealthService:
    """Report application dependency health without leaking probes into HTTP."""

    def __init__(
        self,
        database: Database,
        runner_path: Path,
        executable_probe: ExecutableProbe | None = None,
        *,
        execution_mode: Literal["embedded", "external"] = "embedded",
        runtime_repository_path: Path | None = None,
        repository_probe: RepositoryProbe | None = None,
    ) -> None:
        self.database = database
        self.runner_path = runner_path
        self.executable_probe = executable_probe or ExecutableProbe()
        self.execution_mode = execution_mode
        self.runtime_repository_path = runtime_repository_path
        self.repository_probe = repository_probe or RepositoryProbe()

    def status(self) -> dict[str, str | bool]:
        database_ok = self.database.ping()
        runner_available = self.executable_probe.available(self.runner_path)
        repository_available = (
            self.runtime_repository_path is not None
            and self.repository_probe.available(self.runtime_repository_path)
        )
        return {
            "status": "ok" if database_ok else "degraded",
            "runner_available": runner_available,
            "runner_required": self.execution_mode == "embedded",
            "worker_mode": self.execution_mode,
            "runtime_repository": "ready" if repository_available else "unavailable",
            "database": "ok" if database_ok else "error",
        }
