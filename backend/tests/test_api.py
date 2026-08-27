from __future__ import annotations

import time

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
        response = client.post(
            "/api/runs",
            json={"scenario": "../secret.yaml", "autopilot": "primary", "commit_sha": "abc123"},
        )
        assert response.status_code == 422


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

