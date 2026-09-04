from __future__ import annotations

import asyncio
import json
import re
from collections.abc import Callable
from pathlib import Path

from app.config.settings import Settings
from app.domain.deployment import BranchDeployment
from app.domain.errors import DeploymentOperationError
from app.infrastructure.deployment.filesystem import DeploymentFileStore
from app.infrastructure.deployment.verifier import (
    DeploymentVerificationError,
    DeploymentVerifier,
)

CommandRunner = Callable[[list[str], Path | None, float], None]


class DeploymentRuntimeAdapter:
    """Own Compose, Caddy, host/container paths, and deployment probes."""

    def __init__(
        self,
        settings: Settings,
        files: DeploymentFileStore,
        command_runner: CommandRunner,
        verifier: DeploymentVerifier,
        worktree_root: Path,
    ) -> None:
        self.settings = settings
        self.files = files
        self.command_runner = command_runner
        self.verifier = verifier
        self.worktree_root = worktree_root.resolve()

    def ensure_caddy_config(self) -> None:
        content = (
            "{\n"
            "\tadmin 127.0.0.1:2019\n"
            "}\n\n"
            f"import {self.files.fragments_dir}/*.caddy\n"
        )
        self.files.ensure_caddy_config(content)

    def write_compose_override(self, deployment: BranchDeployment) -> Path:
        if deployment.frontend_port is None or deployment.backend_port is None:
            raise DeploymentOperationError("deployment ports have not been allocated")
        directory = self.files.prepare_deployment_dir(deployment.id)
        content = f"""services:
  backend:
    environment:
      JSB1_DATA_DIR: /data
      JSB1_DATABASE_PATH: /data/jsb1.db
      JSB1_REPOSITORY_ROOT: /data/repositories
      JSB1_WORKTREE_ROOT: /data/worktrees
      JSB1_BUILD_ROOT: /data/builds
    ports:
      - {json.dumps(f"127.0.0.1:{deployment.backend_port}:8000")}
    volumes:
      - deployment-data:/data
  web:
    ports: !override
      - {json.dumps(f"127.0.0.1:{deployment.frontend_port}:8080")}
  tunnel:
    profiles: [manual-tunnel]

volumes:
  deployment-data:
"""
        path = directory / "compose.override.yaml"
        self.files.write_text(path, content)
        return path

    def write_caddy_fragment(self, deployment: BranchDeployment) -> Path:
        if deployment.frontend_port is None:
            raise DeploymentOperationError("deployment frontend port is not allocated")
        if self.settings.tls_cert_path is None or self.settings.tls_key_path is None:
            raise DeploymentOperationError("deployment TLS paths are not configured")
        cert = str(self.settings.tls_cert_path.expanduser().resolve())
        key = str(self.settings.tls_key_path.expanduser().resolve())
        content = (
            f"{deployment.hostname} {{\n"
            f"\ttls {json.dumps(cert)} {json.dumps(key)}\n"
            f"\treverse_proxy {self.settings.caddy_upstream_host}:{deployment.frontend_port}\n"
            "}\n"
        )
        path = self.fragment_path(deployment.slug)
        self.files.write_text(path, content)
        return path

    async def reload_caddy(self) -> None:
        prefix: list[str] = []
        if self.settings.caddy_container is not None:
            prefix = [
                self.settings.docker_binary,
                "exec",
                self.settings.caddy_container,
            ]
        for operation in ("validate", "reload"):
            await asyncio.to_thread(
                self.command_runner,
                [
                    *prefix,
                    self.settings.caddy_binary,
                    operation,
                    "--config",
                    str(self.files.caddy_config),
                    "--adapter",
                    "caddyfile",
                ],
                None,
                30.0,
            )

    async def compose(
        self, deployment: BranchDeployment, args: list[str], override: Path
    ) -> None:
        worktree = self.worktree(deployment)
        base = worktree / "compose.yaml"
        if not self.files.is_file(base):
            raise DeploymentOperationError("deployment revision has no compose.yaml")
        command = [
            self.settings.docker_binary,
            "compose",
            "-p",
            deployment.compose_project,
            "-f",
            str(base),
            "-f",
            str(override),
            *args,
        ]
        await asyncio.to_thread(
            self.command_runner,
            command,
            worktree,
            self.settings.deployment_command_timeout_sec,
        )

    async def compose_down(self, deployment: BranchDeployment) -> None:
        override = self.override_path(deployment)
        if not self.files.is_file(override):
            return
        await self.compose(deployment, ["down", "--remove-orphans"], override)

    async def wait_for_http(self, deployment: BranchDeployment) -> None:
        try:
            await self.verifier.wait_for_http(deployment)
        except DeploymentVerificationError as exc:
            raise DeploymentOperationError(str(exc)) from exc

    async def wait_for_https(self, hostname: str) -> None:
        try:
            await self.verifier.wait_for_https(hostname)
        except DeploymentVerificationError as exc:
            raise DeploymentOperationError(str(exc)) from exc

    def assert_ports_free(self, deployment: BranchDeployment) -> None:
        try:
            self.verifier.assert_ports_free(deployment)
        except DeploymentVerificationError as exc:
            raise DeploymentOperationError(str(exc)) from exc

    def deployment_dir(self, deployment: BranchDeployment) -> Path:
        try:
            return self.files.deployment_dir(deployment.id)
        except ValueError as exc:
            raise DeploymentOperationError(str(exc)) from exc

    def worktree(self, deployment: BranchDeployment) -> Path:
        candidate = Path(deployment.worktree_path).resolve()
        try:
            candidate.relative_to(self.worktree_root)
        except ValueError as exc:
            raise DeploymentOperationError(
                "deployment worktree escapes configured root"
            ) from exc
        return candidate

    def override_path(self, deployment: BranchDeployment) -> Path:
        return self.files.override_path(deployment.id)

    def fragment_path(self, slug: str) -> Path:
        if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", slug):
            raise DeploymentOperationError("invalid deployment slug")
        return self.files.fragment_path(slug)

    def remove_caddy_fragment(self, slug: str) -> None:
        self.files.remove_fragment(slug)

    def read_caddy_fragment(self, slug: str) -> str | None:
        return self.files.read_if_file(self.fragment_path(slug))

    def restore_caddy_fragment(self, slug: str, content: str | None) -> None:
        if content is None:
            self.remove_caddy_fragment(slug)
            return
        self.files.write_text(self.fragment_path(slug), content)

    def occupied_ports(self) -> set[int]:
        return self.verifier.occupied_ports()
