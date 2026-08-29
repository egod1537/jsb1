from __future__ import annotations

import os
import subprocess
from pathlib import Path

import pytest

from app.domain.build import BuildStatus
from app.domain.repository import RepositoryCreate
from app.repositories.builds import BuildRepository
from app.repositories.database import Database
from app.repositories.instances import InstanceRepository
from app.repositories.jsb_repository_repository import JsbRepositoryRepository
from app.repositories.runs import RunRepository
from app.services.build_manager import BuildManager
from app.services.repository_manager import InvalidRepositoryPath, RepositoryManager


def command(*args: str, cwd: Path) -> str:
    result = subprocess.run(
        list(args), cwd=cwd, check=True, capture_output=True, text=True
    )
    return result.stdout.strip()


def create_git_fixture(source: Path) -> tuple[str, str]:
    source.mkdir(parents=True)
    command("git", "init", "-b", "main", cwd=source)
    command("git", "config", "user.email", "jsb1@example.test", cwd=source)
    command("git", "config", "user.name", "JSB1 Test", cwd=source)
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.16)\nproject(jsb0 NONE)\n",
        encoding="utf-8",
    )
    (source / "version.txt").write_text("A\n", encoding="utf-8")
    command("git", "add", ".", cwd=source)
    command("git", "commit", "-m", "commit A", cwd=source)
    first = command("git", "rev-parse", "HEAD", cwd=source)
    command("git", "switch", "-c", "impl", cwd=source)
    (source / "version.txt").write_text("B\n", encoding="utf-8")
    command("git", "add", ".", cwd=source)
    command("git", "commit", "-m", "commit B", cwd=source)
    second = command("git", "rev-parse", "HEAD", cwd=source)
    return first, second


def pipeline(tmp_path: Path) -> tuple[RepositoryManager, BuildRepository, BuildManager]:
    migrations = Path(__file__).resolve().parents[1] / "migrations"
    database = Database(tmp_path / "data" / "test.db", migrations)
    database.initialize()
    records = JsbRepositoryRepository(database)
    builds = BuildRepository(database)
    manager = RepositoryManager(
        records, tmp_path / "data" / "repositories", tmp_path / "data" / "worktrees"
    )
    build_manager = BuildManager(
        builds,
        manager,
        tmp_path / "data" / "builds",
        executable_relative_path=Path("jsb-sim-runner"),
        build_jobs=2,
        timeout_sec=10,
    )
    return manager, builds, build_manager


def install_fake_cmake(directory: Path) -> Path:
    binary = directory / "cmake"
    binary.write_text(
        """#!/bin/sh
set -eu
if [ "${FAKE_CMAKE_FAIL:-0}" = "1" ]; then
  echo "configured failure" >&2
  exit 9
fi
if [ "${1:-}" = "--build" ]; then
  build_dir="$2"
  printf '#!/bin/sh\\nexit 0\\n' > "$build_dir/jsb-sim-runner"
  chmod +x "$build_dir/jsb-sim-runner"
fi
""",
        encoding="utf-8",
    )
    binary.chmod(0o755)
    return binary


def test_repository_status_revision_worktrees_and_path_safety(tmp_path: Path) -> None:
    manager, _, _ = pipeline(tmp_path)
    source = manager.repository_root / "jsb0"
    first, second = create_git_fixture(source)
    registered = manager.register(
        RepositoryCreate(name="jsb0", remote_url="local-fixture", local_path="jsb0")
    )
    assert registered.current_branch == "impl"
    assert registered.head_commit == second
    assert {branch.name for branch in manager.branches(registered.id)} >= {"main", "impl"}
    assert manager.revision(registered.id, "main").commit_sha == first
    assert manager.revision(registered.id, "impl").commit_message == "commit B"

    (source / "dirty.txt").write_text("dirty\n", encoding="utf-8")
    assert manager.status(registered.id).dirty is True

    first_tree = manager.prepare_worktree(registered.id, first)
    assert manager.prepare_worktree(registered.id, first) == first_tree
    second_tree = manager.prepare_worktree(registered.id, second)
    assert first_tree != second_tree
    assert command("git", "rev-parse", "HEAD", cwd=first_tree) == first
    assert command("git", "rev-parse", "HEAD", cwd=second_tree) == second

    with pytest.raises(InvalidRepositoryPath):
        manager.register(
            RepositoryCreate(
                name="escape", remote_url="local-fixture", local_path="../escape"
            )
        )


@pytest.mark.asyncio
async def test_build_transitions_cache_failure_and_lineage(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    manager, builds, build_manager = pipeline(tmp_path)
    source = manager.repository_root / "jsb0"
    _, second = create_git_fixture(source)
    registered = manager.register(
        RepositoryCreate(name="jsb0", remote_url="local-fixture", local_path="jsb0")
    )
    tools = tmp_path / "tools"
    tools.mkdir()
    install_fake_cmake(tools)
    monkeypatch.setenv("PATH", f"{tools}{os.pathsep}{os.environ['PATH']}")

    build, reused = build_manager.request(registered.id, "impl")
    assert reused is False
    assert build.status is BuildStatus.QUEUED
    await build_manager.execute(build.id)
    completed = builds.get(build.id)
    assert completed.status is BuildStatus.COMPLETED
    assert completed.commit_sha == second
    assert Path(completed.executable_path or "").is_file()

    cached, reused = build_manager.request(registered.id, second)
    assert reused is True
    assert cached.id == completed.id

    monkeypatch.setenv("FAKE_CMAKE_FAIL", "1")
    failed, reused = build_manager.request(registered.id, second, rebuild=True)
    assert reused is False
    await build_manager.execute(failed.id)
    failed = builds.get(failed.id)
    assert failed.status is BuildStatus.FAILED
    assert "code 9" in (failed.error_message or "")

    database = builds.database
    runs = RunRepository(database)
    instances = InstanceRepository(database)
    run = runs.create(
        repository_id=registered.id,
        build_id=completed.id,
        commit_sha=completed.commit_sha,
        scenario_name="roll_hold.yaml",
        scenario_path="/scenarios/roll_hold.yaml",
        autopilot="primary",
    )
    instance = instances.create(build_id=completed.id, run_id=run.id)
    lineage = runs.get(run.id)
    assert lineage.repository_name == "jsb0"
    assert lineage.build_id == completed.id
    assert lineage.build_branch == "impl"
    assert lineage.commit_sha == second
    assert instances.get_for_run(run.id).id == instance.id
