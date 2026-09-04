from __future__ import annotations

import shutil
from pathlib import Path


class ExecutableProbe:
    """Infrastructure check for a configured executable path or PATH entry."""

    @staticmethod
    def available(executable: Path) -> bool:
        return executable.is_file() or shutil.which(str(executable)) is not None
