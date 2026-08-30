from __future__ import annotations

import os
import tempfile
from pathlib import Path


class AtomicFileStore:
    """Write complete files without exposing partially written content."""

    def write_text(self, destination: Path, content: str) -> None:
        self.write_bytes(destination, content.encode("utf-8"))

    def write_bytes(self, destination: Path, content: bytes) -> None:
        destination.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary = tempfile.mkstemp(
            prefix=f".{destination.name}.", suffix=".tmp", dir=destination.parent
        )
        try:
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(content)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, destination)
        except Exception:
            try:
                os.unlink(temporary)
            except FileNotFoundError:
                pass
            raise
