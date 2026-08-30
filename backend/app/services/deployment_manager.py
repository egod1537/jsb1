from __future__ import annotations

import asyncio
import hashlib
import json
import logging
import os
import re
import ssl
from collections.abc import Callable
from pathlib import Path
from typing import Any

from app.config.settings import PROJECT_ROOT, Settings
from app.domain.deployment import BranchDeployment, DeploymentStatus
from app.repositories.deployments import (
    DeploymentRepository,
    InvalidDeploymentTransition,
)
from app.services.repository_manager import RepositoryManager
from app.infrastructure.deployment.verifier import (
    DeploymentVerificationError,
    DeploymentVerifier,
)
from app.infrastructure.deployment import ProcessCommandRunner
from app.infrastructure.filesystem import AtomicFileStore


class DeploymentOperationError(RuntimeError):
    pass


class DeploymentConfigurationError(DeploymentOperationError):
    pass


CommandRunner = Callable[[list[str], Path | None, float], None]
LOGGER = logging.getLogger(__name__)


def branch_slug(branch: str, *, max_length: int = 48) -> str:
    """Return a conservative DNS label; collision handling belongs to the manager."""
    value = re.sub(r"[^a-z0-9-]+", "-", branch.lower())
    value = re.sub(r"-+", "-", value).strip("-")
    value = value[:max_length].rstrip("-")
    if not value:
        raise DeploymentOperationError("branch does not produce a valid deployment slug")
    return value


def deployment_hostname(
    slug: str,
    *,
    branch: str,
    base_domain: str,
    main_branch: str = "main",
    main_hostname: str | None = None,
) -> str:
    if branch == main_branch:
        return main_hostname or f"jsb.{base_domain}"
    return f"{slug}-jsb.{base_domain}"


def _dns_pattern_matches(pattern: str, hostname: str) -> bool:
    pattern = pattern.lower().rstrip(".")
    hostname = hostname.lower().rstrip(".")
    if pattern == hostname:
        return True
    if not pattern.startswith("*."):
        return False
    suffix = pattern[2:]
    return hostname.endswith(f".{suffix}") and hostname.count(".") == suffix.count(".") + 1


def validate_tls_paths(
    cert_path: Path | None,
    key_path: Path | None,
    *,
    base_domain: str,
    main_hostname: str | None = None,
    repository_root: Path = PROJECT_ROOT,
) -> tuple[str, ...]:
    if cert_path is None or key_path is None:
        raise DeploymentConfigurationError(
            "JSB1_TLS_CERT_PATH and JSB1_TLS_KEY_PATH must both be configured"
        )
    cert = cert_path.expanduser().resolve()
    key = key_path.expanduser().resolve()
    for path, label in ((cert, "certificate"), (key, "private key")):
        if not path.is_file():
            raise DeploymentConfigurationError(f"TLS {label} path does not exist")
        if not os.access(path, os.R_OK):
            raise DeploymentConfigurationError(f"TLS {label} path is not readable")
        try:
            path.relative_to(repository_root.resolve())
        except ValueError:
            pass
        else:
            raise DeploymentConfigurationError(
                f"TLS {label} must remain outside the repository"
            )
    if key.stat().st_mode & 0o004:
        raise DeploymentConfigurationError("TLS private key must not be world-readable")
    try:
        decoded: dict[str, Any] = ssl._ssl._test_decode_cert(str(cert))  # type: ignore[attr-defined]
    except (OSError, ssl.SSLError, ValueError) as exc:
        raise DeploymentConfigurationError("TLS certificate is not a readable X.509 certificate") from exc
    sans = tuple(
        str(value).lower().rstrip(".")
        for kind, value in decoded.get("subjectAltName", ())
        if kind == "DNS"
    )
    main_hostname = main_hostname or f"jsb.{base_domain}"
    preview_hostname = f"preview.{base_domain}"
    if not any(_dns_pattern_matches(item, main_hostname) for item in sans):
        raise DeploymentConfigurationError(
            f"TLS certificate does not cover {main_hostname}"
        )
    if not any(_dns_pattern_matches(item, preview_hostname) for item in sans):
        raise DeploymentConfigurationError(
            f"TLS certificate does not cover *.{base_domain}"
        )
    return sans


class DeploymentManager:
    def __init__(
        self,
        deployments: DeploymentRepository,
        repositories: RepositoryManager,
        settings: Settings,
        *,
        command_runner: CommandRunner | None = None,
        verifier: DeploymentVerifier | None = None,
        files: AtomicFileStore | None = None,
    ) -> None:
        self.deployments = deployments
        self.repositories = repositories
        self.settings = settings
        self.deployment_root = settings.resolved_deployment_root.resolve()
        self.fragments_dir = settings.resolved_caddy_fragments_dir.resolve()
        self.caddy_config = settings.resolved_caddy_config_path.resolve()
        self.deployment_root.mkdir(parents=True, exist_ok=True)
        self.fragments_dir.mkdir(parents=True, exist_ok=True)
        self._command_runner = command_runner or ProcessCommandRunner()
        self.verifier = verifier or DeploymentVerifier(settings)
        self.files = files or AtomicFileStore()
        self._locks_guard = asyncio.Lock()
        self._locks: dict[tuple[int, str], asyncio.Lock] = {}
        self._tasks: set[asyncio.Task[None]] = set()

    async def request_deploy(self, repository_id: int, branch: str) -> BranchDeployment:
        self.repositories.validate_branch_name(branch)
        lock = await self._lock_for(repository_id, branch)
        async with lock:
            await asyncio.to_thread(self.repositories.fetch, repository_id)
            revision = await asyncio.to_thread(
                self.repositories.resolve_branch, repository_id, branch
            )
            active = self.deployments.active_for_branch(repository_id, branch)
            if active is not None:
                if active.commit_sha == revision.commit_sha:
                    return active
                if active.status in (DeploymentStatus.QUEUED, DeploymentStatus.STARTING):
                    raise DeploymentOperationError("a deployment update is already in progress")
            worktree = await asyncio.to_thread(
                self.repositories.prepare_worktree, repository_id, revision.commit_sha
            )
            previous = self.deployments.latest_for_branch(repository_id, branch)
            slug = self._slug_for(
                repository_id, branch, revision.commit_sha, previous
            )
            hostname = deployment_hostname(
                slug,
                branch=branch,
                base_domain=self.settings.deployment_base_domain,
                main_branch=self.settings.deployment_main_branch,
                main_hostname=self.settings.deployment_main_hostname,
            )
            route_owner = self.deployments.active_for_hostname(hostname)
            if route_owner is not None and not (
                route_owner.repository_id == repository_id
                and route_owner.branch == branch
            ):
                raise DeploymentOperationError(
                    "deployment hostname is already owned by another branch"
                )
            return self.deployments.create(
                repository_id=repository_id,
                branch=branch,
                commit_sha=revision.commit_sha,
                slug=slug,
                hostname=hostname,
                worktree_path=str(worktree),
            )

    async def deploy(self, repository_id: int, branch: str) -> BranchDeployment:
        deployment = await self.request_deploy(repository_id, branch)
        if deployment.status is DeploymentStatus.QUEUED:
            await self.start(deployment.id)
        return self.deployments.get(deployment.id)

    async def submit(self, repository_id: int, branch: str) -> BranchDeployment:
        deployment = await self.request_deploy(repository_id, branch)
        if deployment.status is DeploymentStatus.QUEUED:
            task = asyncio.create_task(self._background_start(deployment.id))
            self._tasks.add(task)
            task.add_done_callback(self._tasks.discard)
        return deployment

    async def _background_start(self, deployment_id: int) -> None:
        try:
            await self.start(deployment_id)
        except Exception:
            # start() records a safe error message for API/UI inspection.
            return

    async def start(self, deployment_id: int) -> BranchDeployment:
        deployment = self.deployments.get(deployment_id)
        lock = await self._lock_for(deployment.repository_id, deployment.branch)
        async with lock:
            deployment = self.deployments.get(deployment_id)
            if deployment.status is not DeploymentStatus.QUEUED:
                return deployment
            previous = self.deployments.active_for_branch(
                deployment.repository_id, deployment.branch
            )
            if previous is not None and previous.id == deployment.id:
                previous = self._previous_running(deployment)
            compose_started = False
            route_written = False
            route_backup: str | None = None
            try:
                self._validate_configuration()
                deployment = self.deployments.reserve_ports(
                    deployment.id,
                    port_start=self.settings.deployment_port_start,
                    port_end=self.settings.deployment_port_end,
                    unavailable_ports=await asyncio.to_thread(self._occupied_ports),
                )
                self._assert_ports_free(deployment)
                override = self._write_compose_override(deployment)
                await self._compose(
                    deployment,
                    ["up", "-d", "--build", "backend", "web"],
                    override,
                )
                compose_started = True
                await self._wait_for_http(deployment)
                fragment_path = self._fragment_path(deployment.slug)
                if fragment_path.is_file():
                    route_backup = fragment_path.read_text(encoding="utf-8")
                self._write_caddy_fragment(deployment)
                route_written = True
                await self._reload_caddy()
                await self._wait_for_https(deployment.hostname)
                running = self.deployments.transition(
                    deployment.id,
                    expected=[DeploymentStatus.STARTING],
                    status=DeploymentStatus.RUNNING,
                )
                if previous is not None and previous.id != deployment.id:
                    try:
                        await self._stop_runtime(previous, remove_route=False)
                    except Exception as cleanup_error:
                        LOGGER.warning(
                            "replacement deployment %s is running but old deployment %s cleanup failed: %s",
                            deployment.id,
                            previous.id,
                            self._safe_error(cleanup_error),
                        )
                return running
            except Exception as exc:
                if route_written:
                    if route_backup is None:
                        self._remove_caddy_fragment(deployment.slug)
                    else:
                        self.files.write_text(
                            self._fragment_path(deployment.slug), route_backup
                        )
                    try:
                        await self._reload_caddy()
                    except Exception:
                        pass
                if compose_started or deployment.status is DeploymentStatus.STARTING:
                    try:
                        await self._compose_down(deployment)
                    except Exception:
                        pass
                message = self._safe_error(exc)
                try:
                    self.deployments.transition(
                        deployment.id,
                        expected=[DeploymentStatus.QUEUED, DeploymentStatus.STARTING],
                        status=DeploymentStatus.FAILED,
                        error_message=message,
                    )
                except InvalidDeploymentTransition:
                    pass
                raise DeploymentOperationError(message) from exc

    async def restart(self, deployment_id: int) -> BranchDeployment:
        deployment = self.deployments.get(deployment_id)
        lock = await self._lock_for(deployment.repository_id, deployment.branch)
        async with lock:
            deployment = self.deployments.transition(
                deployment.id,
                expected=[DeploymentStatus.RUNNING],
                status=DeploymentStatus.STARTING,
            )
            try:
                self._validate_configuration()
                override = self._override_path(deployment)
                await self._compose(deployment, ["restart", "backend", "web"], override)
                await self._wait_for_http(deployment)
                self._write_caddy_fragment(deployment)
                await self._reload_caddy()
                await self._wait_for_https(deployment.hostname)
                return self.deployments.transition(
                    deployment.id,
                    expected=[DeploymentStatus.STARTING],
                    status=DeploymentStatus.RUNNING,
                )
            except Exception as exc:
                self._remove_caddy_fragment(deployment.slug)
                try:
                    await self._reload_caddy()
                except Exception:
                    pass
                try:
                    await self._compose_down(deployment)
                except Exception:
                    pass
                message = self._safe_error(exc)
                self.deployments.transition(
                    deployment.id,
                    expected=[DeploymentStatus.STARTING],
                    status=DeploymentStatus.FAILED,
                    error_message=message,
                )
                raise DeploymentOperationError(message) from exc

    async def stop(self, deployment_id: int, *, force: bool = False) -> BranchDeployment:
        deployment = self.deployments.get(deployment_id)
        if deployment.branch == self.settings.deployment_main_branch and not force:
            raise DeploymentOperationError("stopping main requires force=true")
        lock = await self._lock_for(deployment.repository_id, deployment.branch)
        async with lock:
            current = self.deployments.get(deployment_id)
            if current.status is DeploymentStatus.STOPPED:
                return current
            if current.status is DeploymentStatus.QUEUED:
                return self.deployments.transition(
                    current.id,
                    expected=[current.status],
                    status=DeploymentStatus.STOPPED,
                )
            if current.status is DeploymentStatus.FAILED:
                await self._compose_down(current)
                return self.deployments.transition(
                    current.id,
                    expected=[DeploymentStatus.FAILED],
                    status=DeploymentStatus.STOPPED,
                )
            return await self._stop_runtime(current, remove_route=True)

    def status(self, deployment_id: int) -> BranchDeployment:
        return self.deployments.get(deployment_id)

    async def shutdown(self) -> None:
        if not self._tasks:
            return
        tasks = list(self._tasks)
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)

    async def _stop_runtime(
        self, deployment: BranchDeployment, *, remove_route: bool
    ) -> BranchDeployment:
        current_route = self.deployments.current_for_hostname(deployment.hostname)
        should_remove = remove_route and (
            current_route is None or current_route.id == deployment.id
        )
        if should_remove:
            self._remove_caddy_fragment(deployment.slug)
            await self._reload_caddy()
        await self._compose_down(deployment)
        return self.deployments.transition(
            deployment.id,
            expected=[
                DeploymentStatus.QUEUED,
                DeploymentStatus.STARTING,
                DeploymentStatus.RUNNING,
                DeploymentStatus.FAILED,
            ],
            status=DeploymentStatus.STOPPED,
        )

    def _slug_for(
        self,
        repository_id: int,
        branch: str,
        commit_sha: str,
        active: BranchDeployment | None,
    ) -> str:
        if branch == self.settings.deployment_main_branch:
            return "main"
        if active is not None:
            return active.slug
        base = branch_slug(branch)
        main_suffix = f".{self.settings.deployment_base_domain}"
        main_label = self.settings.deployment_main_hostname.removesuffix(main_suffix)
        owner = self.deployments.slug_owner(
            base, repository_id=repository_id, branch=branch
        )
        if owner is None and base not in {"main", main_label}:
            return base
        candidate = f"{base[:55].rstrip('-')}-{commit_sha[:7].lower()}"
        owner = self.deployments.slug_owner(
            candidate, repository_id=repository_id, branch=branch
        )
        if owner is None:
            return candidate
        digest = hashlib.sha256(branch.encode("utf-8")).hexdigest()[:6]
        return f"{base[:48].rstrip('-')}-{commit_sha[:7].lower()}-{digest}"

    def _validate_configuration(self) -> None:
        domain = self.settings.deployment_base_domain.lower().rstrip(".")
        main_hostname = self.settings.deployment_main_hostname.lower().rstrip(".")
        if not re.fullmatch(
            r"(?=.{1,253}$)(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?",
            domain,
        ):
            raise DeploymentConfigurationError("invalid JSB1_DEPLOYMENT_BASE_DOMAIN")
        if not re.fullmatch(
            r"[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\." + re.escape(domain),
            main_hostname,
        ):
            raise DeploymentConfigurationError(
                "JSB1_DEPLOYMENT_MAIN_HOSTNAME must be exactly one label beneath the base domain"
            )
        if self.settings.deployment_port_start >= self.settings.deployment_port_end:
            raise DeploymentConfigurationError(
                "JSB1_DEPLOYMENT_PORT_START must be lower than JSB1_DEPLOYMENT_PORT_END"
            )
        for value, name in (
            (self.settings.deployment_health_host, "JSB1_DEPLOYMENT_HEALTH_HOST"),
            (
                self.settings.deployment_port_probe_host,
                "JSB1_DEPLOYMENT_PORT_PROBE_HOST",
            ),
            (self.settings.caddy_upstream_host, "JSB1_CADDY_UPSTREAM_HOST"),
            (self.settings.caddy_health_host, "JSB1_CADDY_HEALTH_HOST"),
        ):
            if not re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?", value):
                raise DeploymentConfigurationError(f"invalid {name}")
        if self.settings.caddy_container is not None and not re.fullmatch(
            r"[A-Za-z0-9][A-Za-z0-9_.-]{0,127}", self.settings.caddy_container
        ):
            raise DeploymentConfigurationError("invalid JSB1_CADDY_CONTAINER")
        validate_tls_paths(
            self.settings.tls_cert_path,
            self.settings.tls_key_path,
            base_domain=domain,
            main_hostname=main_hostname,
        )
        self._ensure_caddy_config()

    def _ensure_caddy_config(self) -> None:
        self.caddy_config.parent.mkdir(parents=True, exist_ok=True)
        if self.caddy_config.exists():
            return
        content = (
            "{\n"
            "\tadmin 127.0.0.1:2019\n"
            "}\n\n"
            f"import {self.fragments_dir}/*.caddy\n"
        )
        self.files.write_text(self.caddy_config, content)
        self.files.write_text(self.fragments_dir / "_empty.caddy", "\n")

    def _write_compose_override(self, deployment: BranchDeployment) -> Path:
        if deployment.frontend_port is None or deployment.backend_port is None:
            raise DeploymentOperationError("deployment ports have not been allocated")
        directory = self._deployment_dir(deployment)
        directory.mkdir(parents=True, exist_ok=True)
        content = f"""services:
  backend:
    environment:
      JSB1_DATA_DIR: /data
      JSB1_DATABASE_PATH: /data/jsb1.db
      JSB1_REPOSITORY_ROOT: /data/repositories
      JSB1_WORKTREE_ROOT: /data/worktrees
      JSB1_BUILD_ROOT: /data/builds
    ports:
      - {json.dumps(f'127.0.0.1:{deployment.backend_port}:8000')}
    volumes:
      - deployment-data:/data
  web:
    ports: !override
      - {json.dumps(f'127.0.0.1:{deployment.frontend_port}:8080')}
  tunnel:
    profiles: [manual-tunnel]

volumes:
  deployment-data:
"""
        path = directory / "compose.override.yaml"
        self.files.write_text(path, content)
        return path

    def _write_caddy_fragment(self, deployment: BranchDeployment) -> Path:
        if deployment.frontend_port is None:
            raise DeploymentOperationError("deployment frontend port is not allocated")
        cert = str(self.settings.tls_cert_path.expanduser().resolve())  # type: ignore[union-attr]
        key = str(self.settings.tls_key_path.expanduser().resolve())  # type: ignore[union-attr]
        content = (
            f"{deployment.hostname} {{\n"
            f"\ttls {json.dumps(cert)} {json.dumps(key)}\n"
            f"\treverse_proxy {self.settings.caddy_upstream_host}:{deployment.frontend_port}\n"
            "}\n"
        )
        path = self._fragment_path(deployment.slug)
        self.files.write_text(path, content)
        return path

    async def _reload_caddy(self) -> None:
        prefix: list[str] = []
        if self.settings.caddy_container is not None:
            prefix = [
                self.settings.docker_binary,
                "exec",
                self.settings.caddy_container,
            ]
        await asyncio.to_thread(
            self._command_runner,
            [*prefix,
                self.settings.caddy_binary,
                "validate",
                "--config",
                str(self.caddy_config),
                "--adapter",
                "caddyfile",
            ],
            None,
            30.0,
        )
        await asyncio.to_thread(
            self._command_runner,
            [*prefix,
                self.settings.caddy_binary,
                "reload",
                "--config",
                str(self.caddy_config),
                "--adapter",
                "caddyfile",
            ],
            None,
            30.0,
        )

    async def _compose(
        self, deployment: BranchDeployment, args: list[str], override: Path
    ) -> None:
        worktree = self._worktree(deployment)
        base = worktree / "compose.yaml"
        if not base.is_file():
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
            self._command_runner,
            command,
            worktree,
            self.settings.deployment_command_timeout_sec,
        )

    async def _compose_down(self, deployment: BranchDeployment) -> None:
        override = self._override_path(deployment)
        if not override.is_file():
            return
        await self._compose(
            deployment,
            ["down", "--remove-orphans"],
            override,
        )

    async def _wait_for_http(self, deployment: BranchDeployment) -> None:
        try:
            await self.verifier.wait_for_http(deployment)
        except DeploymentVerificationError as exc:
            raise DeploymentOperationError(str(exc)) from exc

    async def _wait_for_https(self, hostname: str) -> None:
        try:
            await self.verifier.wait_for_https(hostname)
        except DeploymentVerificationError as exc:
            raise DeploymentOperationError(str(exc)) from exc

    def _assert_ports_free(self, deployment: BranchDeployment) -> None:
        try:
            self.verifier.assert_ports_free(deployment)
        except DeploymentVerificationError as exc:
            raise DeploymentOperationError(str(exc)) from exc

    def _occupied_ports(self) -> set[int]:
        return self.verifier.occupied_ports()

    def _previous_running(self, deployment: BranchDeployment) -> BranchDeployment | None:
        for item in self.deployments.list(repository_id=deployment.repository_id):
            if (
                item.id != deployment.id
                and item.branch == deployment.branch
                and item.status is DeploymentStatus.RUNNING
            ):
                return item
        return None

    async def _lock_for(self, repository_id: int, branch: str) -> asyncio.Lock:
        key = (repository_id, branch)
        async with self._locks_guard:
            return self._locks.setdefault(key, asyncio.Lock())

    def _deployment_dir(self, deployment: BranchDeployment) -> Path:
        candidate = (self.deployment_root / str(deployment.id)).resolve()
        try:
            candidate.relative_to(self.deployment_root)
        except ValueError as exc:
            raise DeploymentOperationError("deployment directory escapes configured root") from exc
        return candidate

    def _worktree(self, deployment: BranchDeployment) -> Path:
        candidate = Path(deployment.worktree_path).resolve()
        try:
            candidate.relative_to(self.repositories.worktree_root)
        except ValueError as exc:
            raise DeploymentOperationError("deployment worktree escapes configured root") from exc
        return candidate

    def _override_path(self, deployment: BranchDeployment) -> Path:
        return self._deployment_dir(deployment) / "compose.override.yaml"

    def _fragment_path(self, slug: str) -> Path:
        if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", slug):
            raise DeploymentOperationError("invalid deployment slug")
        return self.fragments_dir / f"{slug}.caddy"

    def _remove_caddy_fragment(self, slug: str) -> None:
        self._fragment_path(slug).unlink(missing_ok=True)

    @staticmethod
    def _safe_error(exc: Exception) -> str:
        message = str(exc).strip() or exc.__class__.__name__
        return message[:2000]
