from __future__ import annotations

import json
import os
import subprocess
import time
from pathlib import Path

from app.domain.build import BuildStatus
from app.domain.repository import RepositoryCreate
from app.repositories.runs import utc_now

from fastapi.testclient import TestClient

from app.main import create_app
from tests.conftest import FakeSimulationRunner


def git(*args: str, cwd: Path) -> str:
    return subprocess.run(
        ["git", *args], cwd=cwd, check=True, capture_output=True, text=True
    ).stdout.strip()


def create_jsb0_fixture(source: Path) -> tuple[str, str]:
    source.mkdir(parents=True)
    git("init", "-b", "main", cwd=source)
    git("config", "user.email", "jsb1@example.test", cwd=source)
    git("config", "user.name", "JSB1 Test", cwd=source)
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.16)\nproject(jsb0 NONE)\n",
        encoding="utf-8",
    )
    (source / "version.txt").write_text("main\n", encoding="utf-8")
    git("add", ".", cwd=source)
    git("commit", "-m", "main", cwd=source)
    git("switch", "-c", "backend", cwd=source)
    (source / "version.txt").write_text("backend-v1\n", encoding="utf-8")
    git("add", ".", cwd=source)
    git("commit", "-m", "backend v1", cwd=source)
    return git("rev-parse", "main", cwd=source), git("rev-parse", "HEAD", cwd=source)


def install_fake_cmake(directory: Path) -> None:
    binary = directory / "cmake"
    binary.write_text(
        """#!/bin/sh
set -eu
if [ "${1:-}" = "--build" ]; then
  build_dir="$2"
  printf '#!/bin/sh\\nexit 0\\n' > "$build_dir/jsb-sim-runner"
  chmod +x "$build_dir/jsb-sim-runner"
fi
""",
        encoding="utf-8",
    )
    binary.chmod(0o755)


def wait_for_terminal(client: TestClient, run_id: int) -> dict:
    for _ in range(100):
        response = client.get(f"/api/runs/{run_id}")
        body = response.json()
        if body["run"]["status"] in {"completed", "failed"}:
            return body
        time.sleep(0.01)
    raise AssertionError("run did not finish")


def test_health_and_invalid_scenario(settings) -> None:
    with TestClient(create_app(settings, FakeSimulationRunner())) as client:
        health = client.get("/api/health")
        assert health.status_code == 200
        assert health.json()["database"] == "ok"
        assert client.get("/api/deployments").json() == []
        response = client.post(
            "/api/runs",
            json={"scenario": "../secret.yaml", "autopilot": "primary", "commit_sha": "abc123"},
        )
        assert response.status_code == 422


def test_main_deployment_stop_requires_force(settings) -> None:
    with TestClient(create_app(settings, FakeSimulationRunner())) as client:
        source = client.app.state.jsb_repositories.create(
            name="jsb1",
            remote_url="https://example.test/jsb1.git",
            local_path="jsb1",
            default_branch="main",
        )
        deployment = client.app.state.deployments.create(
            repository_id=source.id,
            branch="main",
            commit_sha="a" * 40,
            slug="main",
            hostname="jsb.mangagaki.net",
            worktree_path=str(settings.resolved_worktree_root / str(source.id) / ("a" * 40)),
        )
        denied = client.delete(f"/api/deployments/{deployment.id}")
        assert denied.status_code == 422
        stopped = client.delete(f"/api/deployments/{deployment.id}?force=true")
        assert stopped.status_code == 204
        detail = client.get(f"/api/deployments/{deployment.id}")
        assert detail.json()["status"] == "stopped"


def test_successful_run_flow_and_signal_api(settings) -> None:
    with TestClient(create_app(settings, FakeSimulationRunner())) as client:
        response = client.post(
            "/api/runs",
            json={"scenario": "roll_hold_5deg.yaml", "autopilot": "primary", "commit_sha": "abc123"},
        )
        assert response.status_code == 202
        assert response.json()["status"] == "queued"
        run_id = response.json()["id"]
        detail = wait_for_terminal(client, run_id)
        assert detail["run"]["status"] == "completed"
        assert detail["run"]["branch"] is None
        assert detail["run"]["build_id"] is None
        assert len(detail["metrics"]) == 5
        assert {item["kind"] for item in detail["artifacts"]} >= {"telemetry", "metrics", "run", "stdout"}
        metrics = client.get(f"/api/runs/{run_id}/metrics")
        assert "rms_error_deg" in metrics.json()
        signals = client.get(
            f"/api/runs/{run_id}/signals",
            params={"channels": "commanded_roll,roll,aileron", "max_points": 20},
        )
        assert signals.status_code == 200
        assert signals.json()["returned_points"] == 20
        assert signals.json()["units"]["roll"] == "deg"
        artifact = client.get(f"/api/runs/{run_id}/artifacts/telemetry")
        assert artifact.status_code == 200


def test_fake_runner_failure_sets_failed_status(settings) -> None:
    with TestClient(create_app(settings, FakeSimulationRunner(exit_code=3))) as client:
        response = client.post(
            "/api/runs",
            json={"scenario": "roll_hold_5deg.yaml", "autopilot": "primary", "commit_sha": "abc123"},
        )
        detail = wait_for_terminal(client, response.json()["id"])
        assert detail["run"]["status"] == "failed"
        assert detail["run"]["exit_code"] == 3
        assert "exited with code 3" in detail["run"]["error_message"]


def test_build_backed_run_persists_repository_lineage(settings) -> None:
    with TestClient(create_app(settings, FakeSimulationRunner())) as client:
        repository = client.app.state.jsb_repositories.create(
            name="jsb0",
            remote_url="local-fixture",
            local_path="jsb0",
            default_branch="impl",
        )
        build = client.app.state.builds.create(
            repository_id=repository.id,
            commit_sha="a" * 40,
            branch="impl",
            build_dir="",
            stdout_path="",
            stderr_path="",
        )
        build_dir = settings.resolved_build_root / f"{build.id:06d}"
        build_dir.mkdir(parents=True)
        executable = build_dir / "jsb-sim-runner"
        executable.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        executable.chmod(0o755)
        client.app.state.builds.set_paths(
            build.id,
            build_dir=str(build_dir),
            stdout_path=str(build_dir / "stdout.log"),
            stderr_path=str(build_dir / "stderr.log"),
        )
        client.app.state.builds.transition(
            build.id,
            expected=[BuildStatus.QUEUED],
            status=BuildStatus.RUNNING,
            started_at=utc_now(),
        )
        client.app.state.builds.transition(
            build.id,
            expected=[BuildStatus.RUNNING],
            status=BuildStatus.COMPLETED,
            executable_path=str(executable),
            completed_at=utc_now(),
        )

        response = client.post(
            "/api/runs",
            json={
                "scenario": "roll_hold_5deg.yaml",
                "autopilot": "primary",
                "build_id": build.id,
            },
        )
        assert response.status_code == 202
        detail = wait_for_terminal(client, response.json()["id"])
        assert detail["run"]["repository_name"] == "jsb0"
        assert detail["run"]["build_id"] == build.id
        assert detail["run"]["build_branch"] == "impl"
        assert detail["run"]["commit_sha"] == "a" * 40
        assert detail["instance"]["status"] == "stopped"
        manifest = settings.runs_dir / f"{response.json()['id']:06d}" / "run.json"
        payload = json.loads(manifest.read_text(encoding="utf-8"))
        assert payload["repository"] == {"id": repository.id, "name": "jsb0"}
        assert payload["build_id"] == build.id


def test_runtime_repository_configuration_is_required_for_branch_runs(settings) -> None:
    with TestClient(create_app(settings, FakeSimulationRunner())) as client:
        repository = client.get("/api/runtime/repository")
        assert repository.status_code == 503
        assert repository.json()["detail"] == "JSB0 Runtime repository is not configured"
        branches = client.get("/api/runtime/branches")
        assert branches.status_code == 503
        response = client.post(
            "/api/runs",
            json={
                "branch": "backend",
                "scenario": "roll_hold_5deg.yaml",
                "autopilot": "baseline",
            },
        )
        assert response.status_code == 503
        assert response.json()["detail"] == "JSB0 Runtime repository is not configured"


def test_branch_run_resolves_revision_reuses_build_and_preserves_history(
    settings, tmp_path: Path, monkeypatch
) -> None:
    with TestClient(create_app(settings, FakeSimulationRunner())) as client:
        source = settings.resolved_repository_root / "jsb0"
        _, backend_v1 = create_jsb0_fixture(source)
        registered = client.app.state.repository_manager.register(
            RepositoryCreate(
                name="jsb0",
                remote_url="local-fixture",
                local_path="jsb0",
                default_branch="main",
            )
        )
        tools = tmp_path / "tools"
        tools.mkdir()
        install_fake_cmake(tools)
        monkeypatch.setenv("PATH", f"{tools}{os.pathsep}{os.environ['PATH']}")

        assert client.get("/api/autopilots").json()[:2] == ["baseline", "primary"]
        runtime = client.get("/api/runtime/repository")
        assert runtime.status_code == 200
        assert runtime.json()["id"] == registered.id
        assert runtime.json()["name"] == "jsb0"
        assert {item["name"] for item in client.get("/api/runtime/branches").json()} >= {
            "backend",
            "main",
        }
        missing = client.post(
            "/api/runs",
            json={
                "branch": "missing",
                "scenario": "roll_hold_5deg.yaml",
                "autopilot": "baseline",
            },
        )
        assert missing.status_code == 404
        assert missing.json()["detail"] == "Branch no longer exists"

        baseline = client.post(
            "/api/runs",
            json={
                "branch": "backend",
                "scenario": "roll_hold_5deg.yaml",
                "autopilot": "baseline",
            },
        )
        assert baseline.status_code == 202
        baseline_payload = baseline.json()
        assert baseline_payload["branch"] == "backend"
        assert baseline_payload["commit_sha"] == backend_v1
        assert baseline_payload["build_reused"] is False
        baseline_detail = wait_for_terminal(client, baseline_payload["id"])
        assert baseline_detail["run"]["status"] == "completed"
        assert baseline_detail["run"]["repository_id"] == registered.id
        assert baseline_detail["run"]["repository_name"] == "jsb0"
        assert baseline_detail["run"]["branch"] == "backend"
        assert baseline_detail["run"]["commit_sha"] == backend_v1
        assert baseline_detail["run"]["build_branch"] == "backend"
        assert baseline_detail["run"]["autopilot"] == "baseline"

        primary = client.post(
            "/api/runs",
            json={
                "branch": "backend",
                "scenario": "roll_hold_5deg.yaml",
                "autopilot": "primary",
            },
        )
        primary_payload = primary.json()
        assert primary_payload["commit_sha"] == backend_v1
        assert primary_payload["build_id"] == baseline_payload["build_id"]
        assert primary_payload["build_reused"] is True
        primary_detail = wait_for_terminal(client, primary_payload["id"])
        assert primary_detail["run"]["autopilot"] == "primary"

        legacy = client.post(
            "/api/runs",
            json={
                "repository_id": registered.id,
                "branch": "backend",
                "scenario": "roll_hold_5deg.yaml",
                "autopilot": "baseline",
            },
        )
        assert legacy.status_code == 202
        assert legacy.json()["build_reused"] is True
        assert wait_for_terminal(client, legacy.json()["id"])["run"]["repository_id"] == registered.id

        (source / "version.txt").write_text("backend-v2\n", encoding="utf-8")
        git("add", ".", cwd=source)
        git("commit", "-m", "backend v2", cwd=source)
        backend_v2 = git("rev-parse", "HEAD", cwd=source)
        advanced = client.post(
            "/api/runs",
            json={
                "branch": "backend",
                "scenario": "roll_hold_5deg.yaml",
                "autopilot": "baseline",
            },
        )
        advanced_payload = advanced.json()
        assert advanced_payload["commit_sha"] == backend_v2
        assert advanced_payload["build_id"] != baseline_payload["build_id"]
        assert client.get(f"/api/runs/{baseline_payload['id']}").json()["run"]["commit_sha"] == backend_v1
        assert wait_for_terminal(client, advanced_payload["id"])["run"]["status"] == "completed"
