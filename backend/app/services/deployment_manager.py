from __future__ import annotations

import asyncio
import logging
from collections.abc import Callable
from pathlib import Path

from app.config.deployment import DeploymentConfigurationValidator
from app.config.settings import PROJECT_ROOT, Settings
from app.domain.deployment import BranchDeployment, DeploymentStatus
from app.domain.errors import (
    DeploymentConfigurationError,
    DeploymentOperationError,
    InvalidDeploymentTransition,
)
from app.infrastructure.deployment import (
    DeploymentFileStore,
    DeploymentRuntimeAdapter,
    ProcessCommandRunner,
)
from app.infrastructure.deployment import (
    validate_tls_paths as validate_deployment_tls_paths,
)
from app.infrastructure.deployment.verifier import DeploymentVerifier
from app.infrastructure.filesystem import AtomicFileStore
from app.repositories.deployments import DeploymentRepository
from app.services.deployment_planner import (
    DeploymentPlanner,
    branch_slug,
    deployment_hostname,
)
from app.services.repository_manager import RepositoryManager

CommandRunner = Callable[[list[str], Path | None, float], None]
LOGGER = logging.getLogger(__name__)

__all__ = [
    "DeploymentConfigurationError",
    "DeploymentManager",
    "DeploymentOperationError",
    "branch_slug",
    "deployment_hostname",
    "validate_tls_paths",
]


def validate_tls_paths(
    cert_path: Path | None,
    key_path: Path | None,
    *,
    base_domain: str,
    main_hostname: str | None = None,
    repository_root: Path = PROJECT_ROOT,
) -> tuple[str, ...]:
    return validate_deployment_tls_paths(
        cert_path,
        key_path,
        base_domain=base_domain,
        main_hostname=main_hostname,
        repository_root=repository_root,
    )


class DeploymentManager:
    """Persist and orchestrate the legacy controller deployment lifecycle.

    Host deployments and GitHub reporting are owned by deploy.sh; this manager
    intentionally never reads or writes the shell deployment unit state.
    """

    def __init__(
        self,
        deployments: DeploymentRepository,
        repositories: RepositoryManager,
        settings: Settings,
        *,
        command_runner: CommandRunner | None = None,
        verifier: DeploymentVerifier | None = None,
        files: AtomicFileStore | None = None,
        deployment_files: DeploymentFileStore | None = None,
    ) -> None:
        self.deployments = deployments
        self.repositories = repositories
        self.settings = settings
        self.deployment_files = deployment_files or DeploymentFileStore(
            settings.resolved_deployment_root,
            settings.resolved_caddy_fragments_dir,
            settings.resolved_caddy_config_path,
            files,
        )
        self.deployment_root = self.deployment_files.deployment_root
        self.fragments_dir = self.deployment_files.fragments_dir
        self.caddy_config = self.deployment_files.caddy_config
        self._command_runner = command_runner or ProcessCommandRunner()
        self.verifier = verifier or DeploymentVerifier(settings)
        self.files = self.deployment_files.files
        self.configuration = DeploymentConfigurationValidator(settings)
        self.planner = DeploymentPlanner(settings)
        self.runtime = DeploymentRuntimeAdapter(
            settings,
            self.deployment_files,
            self._command_runner,
            self.verifier,
            repositories.worktree_root,
        )
        self._locks_guard = asyncio.Lock()
        self._locks: dict[tuple[int, str], asyncio.Lock] = {}
        self._tasks: set[asyncio.Task[None]] = set()

    def list(
        self, *, repository_id: int | None = None, limit: int = 200
    ) -> list[BranchDeployment]:
        return self.deployments.list(repository_id=repository_id, limit=limit)

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
                if active.status in (
                    DeploymentStatus.QUEUED,
                    DeploymentStatus.STARTING,
                ):
                    raise DeploymentOperationError(
                        "a deployment update is already in progress"
                    )
            worktree = await asyncio.to_thread(
                self.repositories.prepare_worktree, repository_id, revision.commit_sha
            )
            previous = self.deployments.latest_for_branch(repository_id, branch)
            plan = self.planner.plan(
                repository_id=repository_id,
                branch=branch,
                commit_sha=revision.commit_sha,
                previous=previous,
                slug_owner=self.deployments.slug_owner,
            )
            route_owner = self.deployments.active_for_hostname(plan.hostname)
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
                slug=plan.slug,
                hostname=plan.hostname,
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

    async def redeploy(self, deployment_id: int) -> BranchDeployment:
        current = self.status(deployment_id)
        return await self.submit(current.repository_id, current.branch)

    async def _background_start(self, deployment_id: int) -> None:
        try:
            await self.start(deployment_id)
        except Exception:  # noqa: BLE001 - start records the background failure
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
                route_backup = self.runtime.read_caddy_fragment(deployment.slug)
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
                    except Exception as cleanup_error:  # noqa: BLE001 - replacement is live
                        LOGGER.warning(
                            "replacement deployment %s is running but old deployment %s cleanup failed: %s",
                            deployment.id,
                            previous.id,
                            self._safe_error(cleanup_error),
                        )
                return running
            except Exception as exc:
                if route_written:
                    self.runtime.restore_caddy_fragment(deployment.slug, route_backup)
                    try:
                        await self._reload_caddy()
                    except Exception:  # noqa: BLE001, S110 - best-effort rollback
                        pass
                if compose_started or deployment.status is DeploymentStatus.STARTING:
                    try:
                        await self._compose_down(deployment)
                    except Exception:  # noqa: BLE001, S110 - best-effort rollback
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
                except Exception:  # noqa: BLE001, S110 - best-effort rollback
                    pass
                try:
                    await self._compose_down(deployment)
                except Exception:  # noqa: BLE001, S110 - best-effort rollback
                    pass
                message = self._safe_error(exc)
                self.deployments.transition(
                    deployment.id,
                    expected=[DeploymentStatus.STARTING],
                    status=DeploymentStatus.FAILED,
                    error_message=message,
                )
                raise DeploymentOperationError(message) from exc

    async def stop(
        self, deployment_id: int, *, force: bool = False
    ) -> BranchDeployment:
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
        return self.planner.slug_for(
            repository_id=repository_id,
            branch=branch,
            commit_sha=commit_sha,
            previous=active,
            slug_owner=self.deployments.slug_owner,
        )

    def _validate_configuration(self) -> None:
        self.configuration.validate()
        self._ensure_caddy_config()

    def _ensure_caddy_config(self) -> None:
        self.runtime.ensure_caddy_config()

    def _write_compose_override(self, deployment: BranchDeployment) -> Path:
        return self.runtime.write_compose_override(deployment)

    def _write_caddy_fragment(self, deployment: BranchDeployment) -> Path:
        return self.runtime.write_caddy_fragment(deployment)

    async def _reload_caddy(self) -> None:
        await self.runtime.reload_caddy()

    async def _compose(
        self, deployment: BranchDeployment, args: list[str], override: Path
    ) -> None:
        await self.runtime.compose(deployment, args, override)

    async def _compose_down(self, deployment: BranchDeployment) -> None:
        await self.runtime.compose_down(deployment)

    async def _wait_for_http(self, deployment: BranchDeployment) -> None:
        await self.runtime.wait_for_http(deployment)

    async def _wait_for_https(self, hostname: str) -> None:
        await self.runtime.wait_for_https(hostname)

    def _assert_ports_free(self, deployment: BranchDeployment) -> None:
        self.runtime.assert_ports_free(deployment)

    def _occupied_ports(self) -> set[int]:
        return self.runtime.occupied_ports()

    def _previous_running(
        self, deployment: BranchDeployment
    ) -> BranchDeployment | None:
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
        return self.runtime.deployment_dir(deployment)

    def _worktree(self, deployment: BranchDeployment) -> Path:
        return self.runtime.worktree(deployment)

    def _override_path(self, deployment: BranchDeployment) -> Path:
        return self.runtime.override_path(deployment)

    def _fragment_path(self, slug: str) -> Path:
        return self.runtime.fragment_path(slug)

    def _remove_caddy_fragment(self, slug: str) -> None:
        self.runtime.remove_caddy_fragment(slug)

    @staticmethod
    def _safe_error(exc: Exception) -> str:
        message = str(exc).strip() or exc.__class__.__name__
        return message[:2000]
