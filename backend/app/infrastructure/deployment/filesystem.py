from __future__ import annotations

from pathlib import Path

from app.infrastructure.filesystem import AtomicFileStore


class DeploymentFileStore:
    """Filesystem boundary for generated deployment and Caddy configuration."""

    def __init__(
        self,
        deployment_root: Path,
        fragments_dir: Path,
        caddy_config: Path,
        files: AtomicFileStore | None = None,
    ) -> None:
        self.deployment_root = deployment_root.resolve()
        self.fragments_dir = fragments_dir.resolve()
        self.caddy_config = caddy_config.resolve()
        self.files = files or AtomicFileStore()
        self.deployment_root.mkdir(parents=True, exist_ok=True)
        self.fragments_dir.mkdir(parents=True, exist_ok=True)

    def ensure_caddy_config(self, content: str) -> None:
        self.caddy_config.parent.mkdir(parents=True, exist_ok=True)
        if self.caddy_config.exists():
            return
        self.files.write_text(self.caddy_config, content)
        self.files.write_text(self.fragments_dir / "_empty.caddy", "\n")

    def deployment_dir(self, deployment_id: int) -> Path:
        candidate = (self.deployment_root / str(deployment_id)).resolve()
        try:
            candidate.relative_to(self.deployment_root)
        except ValueError as exc:
            raise ValueError("deployment directory escapes configured root") from exc
        return candidate

    def prepare_deployment_dir(self, deployment_id: int) -> Path:
        directory = self.deployment_dir(deployment_id)
        directory.mkdir(parents=True, exist_ok=True)
        return directory

    def override_path(self, deployment_id: int) -> Path:
        return self.deployment_dir(deployment_id) / "compose.override.yaml"

    def fragment_path(self, slug: str) -> Path:
        return self.fragments_dir / f"{slug}.caddy"

    def write_text(self, path: Path, content: str) -> None:
        self.files.write_text(path, content)

    @staticmethod
    def is_file(path: Path) -> bool:
        return path.is_file()

    @staticmethod
    def read_if_file(path: Path) -> str | None:
        if not path.is_file():
            return None
        return path.read_text(encoding="utf-8")

    def remove_fragment(self, slug: str) -> None:
        self.fragment_path(slug).unlink(missing_ok=True)
