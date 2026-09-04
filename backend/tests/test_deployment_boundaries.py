from __future__ import annotations

from pathlib import Path

import pytest

from app.config.deployment import DeploymentConfigurationValidator
from app.config.settings import Settings
from app.domain.errors import DeploymentConfigurationError, DeploymentOperationError
from app.infrastructure.deployment import (
    DeploymentFileStore,
    DeploymentRuntimeAdapter,
    DeploymentVerifier,
)
from app.infrastructure.git import (
    InvalidRepositoryFilesystemPath,
    RepositoryPathResolver,
)
from app.services.deployment_planner import DeploymentPlanner


def settings_for(tmp_path: Path, **updates: object) -> Settings:
    return Settings(
        _env_file=None,
        data_dir=tmp_path / "data",
        deployment_root=tmp_path / "deployments",
        caddy_fragments_dir=tmp_path / "caddy" / "deployments",
        caddy_config_path=tmp_path / "caddy" / "Caddyfile",
        tls_cert_path=tmp_path / "certificate.pem",
        tls_key_path=tmp_path / "certificate.key",
        **updates,
    )


def test_deployment_planner_is_pure_naming_policy(tmp_path: Path) -> None:
    settings = settings_for(tmp_path)
    lookups: list[str] = []

    def no_owner(slug: str, **_: object) -> None:
        lookups.append(slug)

    plan = DeploymentPlanner(settings).plan(
        repository_id=7,
        branch="feature/contracts",
        commit_sha="a" * 40,
        previous=None,
        slug_owner=no_owner,
    )

    assert plan.slug == "feature-contracts"
    assert plan.hostname == "feature-contracts-jsb.mangagaki.net"
    assert lookups == ["feature-contracts"]


def test_deployment_configuration_is_validated_outside_manager(
    tmp_path: Path,
) -> None:
    settings = settings_for(
        tmp_path,
        deployment_port_start=19000,
        deployment_port_end=18000,
    )
    with pytest.raises(DeploymentConfigurationError, match="PORT_START"):
        DeploymentConfigurationValidator(settings).validate()


def test_deployment_runtime_owns_worktree_path_translation(tmp_path: Path) -> None:
    settings = settings_for(tmp_path)
    files = DeploymentFileStore(
        settings.resolved_deployment_root,
        settings.resolved_caddy_fragments_dir,
        settings.resolved_caddy_config_path,
    )
    runtime = DeploymentRuntimeAdapter(
        settings,
        files,
        lambda *_: None,
        DeploymentVerifier(settings),
        tmp_path / "worktrees",
    )
    deployment = type(
        "DeploymentPath",
        (),
        {"worktree_path": str(tmp_path / "outside")},
    )()
    with pytest.raises(DeploymentOperationError, match="escapes configured root"):
        runtime.worktree(deployment)  # type: ignore[arg-type]


def test_repository_path_mapping_keeps_logical_paths_under_root(
    tmp_path: Path,
) -> None:
    resolver = RepositoryPathResolver(tmp_path / "repositories", tmp_path / "worktrees")
    path = resolver.source("jsb0")
    assert path == (tmp_path / "repositories" / "jsb0").resolve()
    assert resolver.stored(path) == "jsb0"
    with pytest.raises(InvalidRepositoryFilesystemPath, match="escape"):
        resolver.source("../outside")
