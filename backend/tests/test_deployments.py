from __future__ import annotations

import asyncio
import ssl
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import pytest

from app.config.settings import Settings
from app.domain.deployment import DeploymentStatus
from app.domain.repository import RepositoryCreate, Revision
from app.repositories.database import Database
from app.repositories.deployments import DeploymentRepository
from app.repositories.jsb_repository_repository import JsbRepositoryRepository
from app.services.deployment_manager import (
    DeploymentConfigurationError,
    DeploymentManager,
    DeploymentOperationError,
    branch_slug,
    deployment_hostname,
    validate_tls_paths,
)
from app.services.repository_manager import GitOperationError, RepositoryManager


def deployment_repository(tmp_path: Path) -> tuple[DeploymentRepository, int]:
    migrations = Path(__file__).resolve().parents[1] / "migrations"
    database = Database(tmp_path / "jsb1.db", migrations)
    database.initialize()
    repositories = JsbRepositoryRepository(database)
    record = repositories.create(
        name="jsb1",
        remote_url="https://example.test/jsb1.git",
        local_path="jsb1",
        default_branch="main",
    )
    return DeploymentRepository(database), record.id


def create_record(
    repository: DeploymentRepository,
    repository_id: int,
    tmp_path: Path,
    *,
    branch: str = "impl",
    commit_sha: str = "a" * 40,
    slug: str = "impl",
) -> int:
    return repository.create(
        repository_id=repository_id,
        branch=branch,
        commit_sha=commit_sha,
        slug=slug,
        hostname=deployment_hostname(
            slug, branch=branch, base_domain="mangagaki.net"
        ),
        worktree_path=str(tmp_path / "worktrees" / str(repository_id) / commit_sha),
    ).id


def test_branch_slug_generation_and_hostname_mapping() -> None:
    assert branch_slug("feature/foo") == "feature-foo"
    assert branch_slug("test_some.branch") == "test-some-branch"
    assert branch_slug("A---B") == "a-b"
    assert len(branch_slug("a" * 100)) == 48
    assert deployment_hostname(
        "main", branch="main", base_domain="mangagaki.net"
    ) == "jsb.mangagaki.net"
    assert deployment_hostname(
        "main",
        branch="main",
        base_domain="example.test",
        main_hostname="preview.example.test",
    ) == "preview.example.test"
    assert deployment_hostname(
        "impl", branch="impl", base_domain="mangagaki.net"
    ) == "impl-jsb.mangagaki.net"
    with pytest.raises(DeploymentOperationError):
        branch_slug("___")


@pytest.mark.parametrize(
    "branch",
    ["-bad", "bad branch", "../escape", "feature//foo", "feature@{one", "topic.lock"],
)
def test_invalid_branch_handling(branch: str) -> None:
    with pytest.raises(GitOperationError):
        RepositoryManager.validate_branch_name(branch)


def test_deployment_state_transitions_and_port_release(tmp_path: Path) -> None:
    repository, repository_id = deployment_repository(tmp_path)
    first_id = create_record(repository, repository_id, tmp_path)
    queued = repository.get(first_id)
    assert queued.status is DeploymentStatus.QUEUED
    starting = repository.reserve_ports(first_id, port_start=18100, port_end=18103)
    assert starting.status is DeploymentStatus.STARTING
    assert (starting.frontend_port, starting.backend_port) == (18100, 18101)
    running = repository.transition(
        first_id,
        expected=[DeploymentStatus.STARTING],
        status=DeploymentStatus.RUNNING,
    )
    assert running.started_at is not None
    repository.transition(
        first_id,
        expected=[DeploymentStatus.RUNNING],
        status=DeploymentStatus.STOPPED,
    )
    second_id = create_record(
        repository,
        repository_id,
        tmp_path,
        branch="feature/x",
        commit_sha="b" * 40,
        slug="feature-x",
    )
    second = repository.reserve_ports(second_id, port_start=18100, port_end=18103)
    assert (second.frontend_port, second.backend_port) == (18100, 18101)


def test_concurrent_port_allocation_is_unique(tmp_path: Path) -> None:
    repository, repository_id = deployment_repository(tmp_path)
    ids = [
        create_record(
            repository,
            repository_id,
            tmp_path,
            branch=f"feature/{index}",
            commit_sha=f"{index:040x}",
            slug=f"feature-{index}",
        )
        for index in range(4)
    ]
    barrier = threading.Barrier(len(ids))

    def reserve(deployment_id: int) -> tuple[int | None, int | None]:
        barrier.wait()
        item = repository.reserve_ports(
            deployment_id, port_start=18200, port_end=18215
        )
        return item.frontend_port, item.backend_port

    with ThreadPoolExecutor(max_workers=len(ids)) as executor:
        pairs = list(executor.map(reserve, ids))
    ports = [port for pair in pairs for port in pair]
    assert None not in ports
    assert len(set(ports)) == len(ports)


def test_tls_path_validation_and_missing_certificate(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    cert = tmp_path / "origin.pem"
    key = tmp_path / "origin.key"
    cert.write_text("public certificate placeholder", encoding="utf-8")
    key.write_text("private key placeholder", encoding="utf-8")
    key.chmod(0o600)
    monkeypatch.setattr(
        ssl._ssl,
        "_test_decode_cert",
        lambda _: {
            "subjectAltName": (
                ("DNS", "jsb.mangagaki.net"),
                ("DNS", "*.mangagaki.net"),
            )
        },
    )
    assert validate_tls_paths(
        cert, key, base_domain="mangagaki.net"
    ) == ("jsb.mangagaki.net", "*.mangagaki.net")
    with pytest.raises(DeploymentConfigurationError, match="does not exist"):
        validate_tls_paths(
            tmp_path / "missing.pem", key, base_domain="mangagaki.net"
        )
    monkeypatch.setattr(
        ssl._ssl,
        "_test_decode_cert",
        lambda _: {"subjectAltName": (("DNS", "*.jsb.mangagaki.net"),)},
    )
    with pytest.raises(DeploymentConfigurationError, match="jsb.mangagaki.net"):
        validate_tls_paths(cert, key, base_domain="mangagaki.net")
    monkeypatch.setattr(
        ssl._ssl,
        "_test_decode_cert",
        lambda _: {"subjectAltName": (("DNS", "jsb.mangagaki.net"),)},
    )
    with pytest.raises(DeploymentConfigurationError, match=r"\*\.mangagaki"):
        validate_tls_paths(cert, key, base_domain="mangagaki.net")


class FakeRepositoryManager:
    def __init__(self, root: Path) -> None:
        self.worktree_root = root.resolve()
        self.commit_sha = "a" * 40
        self.fetch_count = 0

    @staticmethod
    def validate_branch_name(branch: str) -> None:
        RepositoryManager.validate_branch_name(branch)

    def fetch(self, repository_id: int) -> None:
        assert repository_id > 0
        self.fetch_count += 1

    def resolve_branch(self, repository_id: int, branch: str) -> Revision:
        return Revision(
            repository_id=repository_id,
            commit_sha=self.commit_sha,
            branch=branch,
            commit_message="test",
            committed_at="2026-08-28T00:00:00Z",
            dirty=False,
        )

    def prepare_worktree(self, repository_id: int, commit_sha: str) -> Path:
        worktree = self.worktree_root / str(repository_id) / commit_sha
        worktree.mkdir(parents=True, exist_ok=True)
        (worktree / "compose.yaml").write_text("services: {}\n", encoding="utf-8")
        return worktree


def manager_fixture(
    tmp_path: Path,
) -> tuple[DeploymentManager, DeploymentRepository, FakeRepositoryManager, int, list[list[str]]]:
    deployments, repository_id = deployment_repository(tmp_path)
    repositories = FakeRepositoryManager(tmp_path / "worktrees")
    commands: list[list[str]] = []

    def command_runner(command: list[str], cwd: Path | None, timeout: float) -> None:
        assert timeout > 0
        commands.append(command)

    settings = Settings(
        data_dir=tmp_path / "data",
        database_path=tmp_path / "jsb1.db",
        worktree_root=repositories.worktree_root,
        deployment_root=tmp_path / "deployments",
        caddy_fragments_dir=tmp_path / "caddy" / "deployments",
        caddy_config_path=tmp_path / "caddy" / "Caddyfile",
        tls_cert_path=tmp_path / "outside-origin.pem",
        tls_key_path=tmp_path / "outside-origin.key",
        deployment_port_start=18300,
        deployment_port_end=18320,
        deployment_health_timeout_sec=0.1,
        deployment_health_interval_sec=0.01,
    )
    manager = DeploymentManager(
        deployments, repositories, settings, command_runner=command_runner  # type: ignore[arg-type]
    )
    manager._validate_configuration = lambda: manager._ensure_caddy_config()  # type: ignore[method-assign]

    async def healthy(*_: object) -> None:
        return None

    manager._wait_for_http = healthy  # type: ignore[method-assign]
    manager._wait_for_https = healthy  # type: ignore[method-assign]
    return manager, deployments, repositories, repository_id, commands


@pytest.mark.asyncio
async def test_caddy_fragment_and_branch_update_replaces_existing(
    tmp_path: Path,
) -> None:
    manager, deployments, repositories, repository_id, commands = manager_fixture(tmp_path)
    first = await manager.deploy(repository_id, "impl")
    assert first.status is DeploymentStatus.RUNNING
    fragment = manager._fragment_path("impl").read_text(encoding="utf-8")
    assert "impl-jsb.mangagaki.net" in fragment
    assert f"127.0.0.1:{first.frontend_port}" in fragment
    assert "reverse_proxy" in fragment
    override = manager._override_path(first).read_text(encoding="utf-8")
    assert "ports: !override" in override
    assert "deployment-data:/data" in override

    repositories.commit_sha = "b" * 40
    second = await manager.deploy(repository_id, "impl")
    assert second.commit_sha == "b" * 40
    assert second.id != first.id
    assert deployments.get(first.id).status is DeploymentStatus.STOPPED
    assert deployments.get(second.id).status is DeploymentStatus.RUNNING
    assert any("down" in command for command in commands)


@pytest.mark.asyncio
async def test_slug_collision_gets_commit_suffix(tmp_path: Path) -> None:
    manager, _, repositories, repository_id, _ = manager_fixture(tmp_path)
    first = await manager.request_deploy(repository_id, "feature/foo")
    assert first.slug == "feature-foo"
    repositories.commit_sha = "c1b2c3d4" + "0" * 32
    second = await manager.request_deploy(repository_id, "feature-foo")
    assert second.slug == "feature-foo-c1b2c3d"
    repositories.commit_sha = "d1e2f3a4" + "0" * 32
    reserved = await manager.request_deploy(repository_id, "MAIN")
    assert reserved.slug == "main-d1e2f3a"
    repositories.commit_sha = "e1f2a3b4" + "0" * 32
    reserved_hostname = await manager.request_deploy(repository_id, "JSB")
    assert reserved_hostname.slug == "jsb-e1f2a3b"


@pytest.mark.asyncio
async def test_failed_health_check_marks_deployment_failed(tmp_path: Path) -> None:
    manager, deployments, _, repository_id, commands = manager_fixture(tmp_path)

    async def fail_health(*_: object) -> None:
        raise DeploymentOperationError("application health check timed out")

    manager._wait_for_http = fail_health  # type: ignore[method-assign]
    deployment = await manager.request_deploy(repository_id, "impl")
    with pytest.raises(DeploymentOperationError, match="health check"):
        await manager.start(deployment.id)
    failed = deployments.get(deployment.id)
    assert failed.status is DeploymentStatus.FAILED
    assert "health check" in (failed.error_message or "")
    assert any("down" in command for command in commands)


@pytest.mark.asyncio
async def test_same_commit_redeploy_reuses_running_deployment(tmp_path: Path) -> None:
    manager, _, _, repository_id, _ = manager_fixture(tmp_path)
    first = await manager.deploy(repository_id, "impl")
    second = await manager.deploy(repository_id, "impl")
    assert second.id == first.id
