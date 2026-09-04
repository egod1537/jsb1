from __future__ import annotations

import subprocess
from pathlib import Path


class DeploymentCommandError(RuntimeError):
    pass


class ProcessCommandRunner:
    """Launch deployment tools without shell interpolation."""

    def __call__(self, command: list[str], cwd: Path | None, timeout: float) -> None:
        try:
            result = subprocess.run(
                command,
                cwd=cwd,
                check=False,
                capture_output=True,
                text=True,
                timeout=timeout,
                shell=False,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise DeploymentCommandError(
                f"command could not be completed: {command[0]}"
            ) from exc
        if result.returncode != 0:
            lines = result.stderr.strip().splitlines()
            detail = lines[-1][:500] if lines else f"exit code {result.returncode}"
            raise DeploymentCommandError(f"{command[0]} command failed: {detail}")
