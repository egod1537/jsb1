from __future__ import annotations

import subprocess
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from app.config.settings import Settings
from app.main import create_app
from app.repositories.database import Database
from app.repositories.jsb_repository_repository import (
    JsbRepositoryRepository,
    RepositoryConflict,
)
from app.services.repository_manager import (
    GitOperationError,
    RepositoryManager,
    RuntimeRepositoryUnavailable,
)
from tests.conftest import FakeSimulationRunner


def git(*args: str, cwd: Path) -> str:
    return subprocess.run(
        ["git", *args], cwd=cwd, check=True, capture_output=True, text=True
    ).stdout.strip()


def create_repository(path: Path) -> None:
    path.mkdir(parents=True)
    git("init", "-b", "backend", cwd=path)
    git("config", "user.email", "jsb1@example.test", cwd=path)
    git("config", "user.name", "JSB1 Test", cwd=path)
    (path / "runtime.txt").write_text("jsb0\n", encoding="utf-8")
    git("add", ".", cwd=path)
    git("commit", "-m", "runtime", cwd=path)


def manager(tmp_path: Path) -> tuple[RepositoryManager, JsbRepositoryRepository]:
    migrations = Path(__file__).resolve().parents[1] / "migrations"
    database = Database(tmp_path / "data" / "test.db", migrations)
    database.initialize()
    records = JsbRepositoryRepository(database)
    return (
        RepositoryManager(
            records,
            tmp_path / "data" / "repositories",
            tmp_path / "data" / "worktrees",
        ),
        records,
    )


def test_settings_define_canonical_defaults_and_environment_override(
    tmp_path: Path, monkeypatch
) -> None:
    defaults = Settings(_env_file=None, data_dir=tmp_path / "defaults")
    assert defaults.jsb0_repository_url == "https://github.com/egod1537/jsb0.git"
    assert defaults.jsb0_default_branch == "impl"
    assert defaults.resolved_jsb0_repository_path == (
        tmp_path / "defaults" / "repositories" / "jsb0"
    )

    configured = tmp_path / "configured-jsb0"
    monkeypatch.setenv("JSB0_REPOSITORY_URL", "git@github.com:example/jsb0.git")
    monkeypatch.setenv("JSB0_REPOSITORY_PATH", str(configured))
    monkeypatch.setenv("JSB0_DEFAULT_BRANCH", "experiment")
    overridden = Settings(_env_file=None, data_dir=tmp_path / "override")
    assert overridden.jsb0_repository_url == "git@github.com:example/jsb0.git"
    assert overridden.resolved_jsb0_repository_path == configured
    assert overridden.jsb0_default_branch == "experiment"


def test_application_startup_bootstraps_the_runtime_repository(
    settings: Settings, tmp_path: Path
) -> None:
    source = tmp_path / "configured-jsb0"
    create_repository(source)
    configured = settings.model_copy(
        update={
            "bootstrap_runtime_repository": True,
            "jsb0_repository_url": "https://github.com/egod1537/jsb0.git",
            "jsb0_repository_path": source,
            "jsb0_default_branch": "backend",
        }
    )

    for _ in range(2):
        with TestClient(create_app(configured, FakeSimulationRunner())) as client:
            runtime = client.get("/api/runtime/repository")
            assert runtime.status_code == 200
            assert runtime.json()["display_name"] == "egod1537/jsb0"
            assert runtime.json()["default_branch"] == "backend"
            assert client.get("/api/runtime/branches").json()[0]["name"] == "backend"
            assert len(client.app.state.jsb_repositories.list()) == 1


def test_bootstrap_reuses_matching_row_without_duplicates(tmp_path: Path) -> None:
    runtime_manager, records = manager(tmp_path)
    source = runtime_manager.repository_root / "jsb0"
    create_repository(source)
    existing = records.create(
        name="jsb0",
        remote_url="git@github.com:egod1537/jsb0.git",
        local_path="jsb0",
        default_branch="main",
    )

    first = runtime_manager.ensure_runtime_repository(
        "https://github.com/egod1537/jsb0",
        source,
        "backend",
    )
    second = runtime_manager.ensure_runtime_repository(
        "https://github.com/egod1537/jsb0.git",
        source,
        "backend",
    )

    assert first.id == existing.id == second.id
    assert first.status == "ready"
    assert first.display_name == "egod1537/jsb0"
    assert [branch.name for branch in runtime_manager.runtime_branches()] == ["backend"]
    assert records.get(existing.id).remote_url == "https://github.com/egod1537/jsb0.git"
    assert records.get(existing.id).default_branch == "backend"
    assert len(records.list()) == 1
    with pytest.raises(RepositoryConflict, match="cannot be deleted"):
        runtime_manager.delete(existing.id)


def test_missing_default_branch_is_a_visible_warning(tmp_path: Path) -> None:
    runtime_manager, _ = manager(tmp_path)
    source = runtime_manager.repository_root / "jsb0"
    create_repository(source)

    status = runtime_manager.ensure_runtime_repository(
        "https://github.com/egod1537/jsb0.git",
        source,
        "missing",
    )

    assert status.status == "warning"
    assert status.error == "Configured default branch is unavailable: missing"
    assert runtime_manager.runtime_repository().id == status.id


def test_github_remote_forms_share_one_canonical_identity() -> None:
    forms = {
        "https://github.com/egod1537/jsb0",
        "https://github.com/egod1537/jsb0.git",
        "git@github.com:egod1537/jsb0.git",
    }
    assert {
        RepositoryManager.normalize_remote_url(value) for value in forms
    } == {"https://github.com/egod1537/jsb0.git"}

    with pytest.raises(GitOperationError, match="remote_url"):
        RepositoryManager._validate_remote_url("https://")


def test_clone_failure_is_reported_without_losing_canonical_record(
    tmp_path: Path, monkeypatch
) -> None:
    runtime_manager, records = manager(tmp_path)

    def fail_clone(*args, **kwargs):
        raise GitOperationError("git clone JSB0 Runtime repository failed")

    monkeypatch.setattr(runtime_manager, "_run", fail_clone)
    status = runtime_manager.ensure_runtime_repository(
        "https://github.com/egod1537/jsb0.git",
        runtime_manager.repository_root / "jsb0",
        "backend",
    )

    assert status.status == "error"
    assert "clone" in (status.error or "")
    assert len(records.list()) == 1


def test_startup_configuration_error_remains_visible_without_a_row(
    tmp_path: Path,
) -> None:
    runtime_manager, _ = manager(tmp_path)
    error = GitOperationError("invalid configured remote URL")
    runtime_manager.record_runtime_configuration_error(error)

    with pytest.raises(RuntimeRepositoryUnavailable, match=str(error)):
        runtime_manager.runtime_status()


def test_fetch_failure_is_a_specific_runtime_error(tmp_path: Path, monkeypatch) -> None:
    runtime_manager, _ = manager(tmp_path)
    source = runtime_manager.repository_root / "jsb0"
    create_repository(source)
    runtime_manager.ensure_runtime_repository(
        "https://github.com/egod1537/jsb0.git",
        source,
        "backend",
    )

    def fail_fetch(*args, **kwargs):
        raise GitOperationError("git fetch failed")

    monkeypatch.setattr(runtime_manager, "fetch", fail_fetch)
    with pytest.raises(
        RuntimeRepositoryUnavailable,
        match="Could not fetch JSB0 Runtime repository",
    ):
        runtime_manager.fetch_runtime_repository()


def test_fetch_serializes_git_and_refreshes_every_origin_branch(
    tmp_path: Path, monkeypatch
) -> None:
    runtime_manager, _ = manager(tmp_path)
    source = runtime_manager.repository_root / "jsb0"
    create_repository(source)
    status = runtime_manager.ensure_runtime_repository(
        "https://github.com/egod1537/jsb0.git",
        source,
        "backend",
    )
    original_git = runtime_manager._git
    active_fetches = 0
    maximum_active_fetches = 0
    fetch_arguments: list[list[str]] = []
    guard = threading.Lock()

    def observed_git(source_path, arguments, **kwargs):
        nonlocal active_fetches, maximum_active_fetches
        if arguments[0] != "fetch":
            return original_git(source_path, arguments, **kwargs)
        with guard:
            fetch_arguments.append(arguments)
            active_fetches += 1
            maximum_active_fetches = max(maximum_active_fetches, active_fetches)
        time.sleep(0.05)
        with guard:
            active_fetches -= 1
        return ""

    monkeypatch.setattr(runtime_manager, "_git", observed_git)
    with ThreadPoolExecutor(max_workers=2) as executor:
        results = list(executor.map(runtime_manager.fetch, [status.id, status.id]))

    assert all(result.id == status.id for result in results)
    assert maximum_active_fetches == 1
    assert fetch_arguments == [[
        "fetch",
        "--prune",
        "origin",
        "+refs/heads/*:refs/remotes/origin/*",
    ]] * 2


def test_bootstrap_does_not_repoint_an_unrelated_existing_checkout(
    tmp_path: Path,
) -> None:
    runtime_manager, _ = manager(tmp_path)
    source = runtime_manager.repository_root / "jsb0"
    create_repository(source)
    unrelated = "https://github.com/egod1537/jsb1.git"
    git("remote", "add", "origin", unrelated, cwd=source)

    status = runtime_manager.ensure_runtime_repository(
        "https://github.com/egod1537/jsb0.git",
        source,
        "backend",
    )

    assert status.status == "error"
    assert status.error == "existing repository origin does not match JSB0_REPOSITORY_URL"
    assert git("remote", "get-url", "origin", cwd=source) == unrelated
