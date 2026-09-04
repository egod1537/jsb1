from __future__ import annotations

import os
import subprocess
from collections.abc import Callable
from pathlib import Path

from app.domain.errors import ExternalProcessError


class GitOperationError(ExternalProcessError):
    pass


class GitRepositoryAdapter:
    """The only JSB1 component that formats and launches Git commands."""

    @staticmethod
    def ensure_directory(path: Path) -> None:
        path.mkdir(parents=True, exist_ok=True)

    def clone(
        self,
        remote_url: str,
        destination: Path,
        *,
        operation: str = "clone",
        timeout: float = 300,
        runner: Callable[..., str] | None = None,
    ) -> str:
        self.ensure_directory(destination.parent)
        execute = runner or self.run
        return execute(
            [
                "git",
                "clone",
                "--origin",
                "origin",
                "--",
                remote_url,
                str(destination),
            ],
            operation=operation,
            cwd=destination.parent,
            timeout=timeout,
        )

    def git(
        self,
        source: Path,
        args: list[str],
        *,
        operation: str,
        timeout: float = 60,
    ) -> str:
        return self.run(
            ["git", "-C", str(source), *args],
            operation=operation,
            timeout=timeout,
        )

    def optional(self, source: Path, args: list[str]) -> str:
        try:
            return self.git(source, args, operation="read repository state")
        except GitOperationError:
            return ""

    def run(
        self,
        command: list[str],
        *,
        operation: str,
        cwd: Path | None = None,
        timeout: float,
    ) -> str:
        environment = os.environ.copy()
        environment["GIT_TERMINAL_PROMPT"] = "0"
        try:
            result = subprocess.run(
                command,
                cwd=cwd,
                env=environment,
                check=False,
                capture_output=True,
                text=True,
                timeout=timeout,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise GitOperationError(f"git {operation} could not be completed") from exc
        if result.returncode != 0:
            detail = result.stderr.strip().splitlines()
            suffix = f": {detail[-1][:500]}" if detail else ""
            raise GitOperationError(f"git {operation} failed{suffix}")
        return result.stdout.strip()
