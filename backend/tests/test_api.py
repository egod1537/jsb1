from __future__ import annotations

import json
import time

from app.domain.build import BuildStatus
from app.repositories.runs import utc_now

from fastapi.testclient import TestClient

from app.main import create_app
from tests.conftest import FakeSimulationRunner


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
