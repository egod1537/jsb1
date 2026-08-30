from __future__ import annotations

import json
import os
import subprocess
import time
from pathlib import Path

from app.domain.build import BuildStatus
from app.domain.models import Metric, RunStatus
from app.domain.repository import RepositoryCreate
from app.main import create_app
from app.repositories.runs import utc_now
from app.services.scenario_sync import CompatibilityContract
from app.services.scenario_validator import ScenarioRuntime
from fastapi.testclient import TestClient

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
    schema_dir = source / "contract" / "scenario"
    schema_dir.mkdir(parents=True)
    (schema_dir / "scenario.schema.json").write_text(
        json.dumps(
            {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "required": ["autopilot"],
                "properties": {
                    "autopilot": {
                        "type": "string",
                        "enum": ["primary", "baseline"],
                    }
                },
            }
        ),
        encoding="utf-8",
    )
    execution_dir = source / "contract" / "execution"
    execution_dir.mkdir(parents=True)
    (execution_dir / "capabilities.json").write_text(
        json.dumps({
            "mode": "compare",
            "variants": ["baseline", "primary"],
        }),
        encoding="utf-8",
    )
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
            json={"scenario": "../secret.yaml", "commit_sha": "abc123"},
        )
        assert response.status_code == 422


def test_execution_variant_is_run_input_with_legacy_scenario_fallback(settings) -> None:
    missing = settings.scenario_dir / "missing_autopilot.yaml"
    missing.write_text("name: Missing autopilot\n", encoding="utf-8")
    with TestClient(create_app(settings, FakeSimulationRunner())) as client:
        missing_response = client.post(
            "/api/runs",
            json={"scenario": missing.name, "commit_sha": "abc123"},
        )
        assert missing_response.status_code == 422
        assert missing_response.json()["detail"] == "Execution variant is required"

        explicit_variant = client.post(
            "/api/runs",
            json={
                "scenario": "roll_hold_5deg_baseline.yaml",
                "variant": "primary",
                "commit_sha": "abc123",
            },
        )
        assert explicit_variant.status_code == 202
        explicit_detail = wait_for_terminal(client, explicit_variant.json()["id"])
        assert explicit_detail["run"]["execution_variant"] == "primary"

        matching_legacy = client.post(
            "/api/runs",
            json={
                "scenario": "roll_hold_5deg.yaml",
                "autopilot": "primary",
                "commit_sha": "abc123",
            },
        )
        assert matching_legacy.status_code == 202
        detail = wait_for_terminal(client, matching_legacy.json()["id"])
        assert detail["run"]["autopilot"] == "primary"
        assert detail["run"]["execution_variant"] == "primary"

        conflict = client.post(
            "/api/runs",
            json={
                "scenario": "roll_hold_5deg.yaml",
                "variant": "primary",
                "autopilot": "baseline",
                "commit_sha": "abc123",
            },
        )
        assert conflict.status_code == 422


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
            json={"scenario": "roll_hold_5deg.yaml", "commit_sha": "abc123"},
        )
        assert response.status_code == 202
        assert response.json()["status"] == "queued"
        run_id = response.json()["id"]
        detail = wait_for_terminal(client, run_id)
        assert detail["run"]["status"] == "completed"
        assert detail["run"]["execution_mode"] == "compare"
        assert detail["run"]["variants"] == ["baseline", "primary"]
        assert set(detail["run"]["variant_results"]) == {"baseline", "primary"}
        assert detail["run"]["branch"] is None
        assert detail["run"]["build_id"] is None
        assert len(detail["metrics"]) == 5
        assert {item["kind"] for item in detail["artifacts"]} >= {"telemetry", "metrics", "run", "stdout", "scenario"}
        assert detail["run"]["scenario_source"] == "bundled"
        assert detail["run"]["scenario_type"] == "roll_hold"
        assert detail["run"]["scenario_sha256"]
        assert detail["run"]["current_stage"] == "complete"
        assert [stage["id"] for stage in detail["run"]["stages"]] == [
            "resolve_scenario",
            "resolve_runtime_revision",
            "validate_contract",
            "resolve_build",
            "freeze_scenario",
            "launch_runner",
            "record_telemetry",
            "collect_artifacts",
            "complete",
        ]
        assert detail["run"]["stages"][-1]["status"] == "success"
        snapshot = settings.runs_dir / f"{run_id:06d}" / "scenario.yaml"
        assert snapshot.is_file()
        metrics = client.get(f"/api/runs/{run_id}/metrics")
        assert "rms_error_deg" in metrics.json()
        analysis = client.get(f"/api/runs/{run_id}/analysis/roll-hold")
        assert analysis.status_code == 200
        analysis_body = analysis.json()
        assert set(analysis_body["variants"]) == {"baseline", "primary"}
        analysis_body = analysis_body["variants"]["primary"]
        assert analysis_body["analyzer"] == "roll_hold"
        assert analysis_body["parameters"]["command_start_sec"] == 5
        assert analysis_body["parameters"]["settling_band_source"] == "scenario"
        assert set(analysis_body["metrics"]) >= {
            "rise_time_s",
            "settling_time_s",
            "overshoot_deg",
            "steady_state_error_deg",
            "rms_tracking_error_deg",
            "peak_roll_rate_deg_s",
            "oscillation_count",
            "residual_oscillation_pp_deg",
            "dominant_oscillation_period_s",
            "peak_aileron",
            "rms_aileron",
            "aileron_saturation_time_s",
        }
        assert analysis_body["targets"]["settling_time_s"] == {
            "value": 10.0,
            "unit": "s",
            "source": "scenario",
        }
        assert analysis_body["targets"]["aileron_saturation_time_s"]["source"] == "unavailable"
        assert analysis_body["regions"]["response"]["start_sec"] == 5
        assert analysis_body["markers"]["command"]["time_sec"] == 5
        assert {check["id"] for check in analysis_body["checks"]} == {
            "rise_time",
            "settling_time",
            "overshoot",
            "steady_state_error",
            "rms_tracking_error",
            "oscillation",
            "residual_oscillation",
            "dominant_period",
            "peak_aileron",
            "rms_aileron",
            "saturation_time",
        }
        signals = client.get(
            f"/api/runs/{run_id}/signals",
            params={"channels": "commanded_roll,roll,aileron", "max_points": 20},
        )
        assert signals.status_code == 200
        assert signals.json()["returned_points"] == 20
        assert signals.json()["units"]["roll"] == "deg"
        assert signals.json()["units"]["aileron"] == "normalized"
        assert max(abs(value) for value in signals.json()["series"]["aileron"]) <= 0.2
        available = client.get(f"/api/runs/{run_id}/signals/available")
        assert available.status_code == 200
        assert set(available.json()["variants"]) == {"baseline", "primary"}
        metadata = {item["name"]: item for item in available.json()["signals"]}
        assert metadata["roll"] == {
            "name": "roll",
            "unit": "deg",
            "display_name": "Roll",
            "symbol": "φ",
            "symbol_latex": "\\phi",
            "category": "Aircraft State",
            "subcategory": "Attitude",
        }
        assert metadata["aileron"]["unit"] == "normalized"
        assert set(metadata) >= {"commanded_roll", "roll", "roll_rate", "aileron"}
        primary_roll = client.get(
            f"/api/runs/{run_id}/signals",
            params={"variant": "primary", "signals": "roll"},
        ).json()["series"]["roll"]
        baseline_roll = client.get(
            f"/api/runs/{run_id}/signals",
            params={"variant": "baseline", "signals": "roll"},
        ).json()["series"]["roll"]
        assert primary_roll != baseline_roll
        artifact = client.get(f"/api/runs/{run_id}/artifacts/telemetry")
        assert artifact.status_code == 200


def test_delete_terminal_run_cleans_owned_state_and_preserves_shared_build(settings) -> None:
    with TestClient(create_app(settings, FakeSimulationRunner())) as client:
        source = client.app.state.jsb_repositories.create(
            name="jsb0-delete-test",
            remote_url="https://example.test/jsb0.git",
            local_path="jsb0-delete-test",
            default_branch="main",
        )
        build = client.app.state.builds.create(
            repository_id=source.id,
            commit_sha="a" * 40,
            branch="main",
            build_dir="builds/000001",
            stdout_path="builds/000001/stdout.log",
            stderr_path="builds/000001/stderr.log",
        )
        runs = client.app.state.repository
        run = runs.create(
            repository_id=source.id,
            build_id=build.id,
            commit_sha="a" * 40,
            scenario_name="Delete me",
            scenario_path="scenario.yaml",
            autopilot="primary",
        )
        runs.set_output_directory(run.id, f"runs/{run.id:06d}")
        run_directory = settings.runs_dir / f"{run.id:06d}"
        run_directory.mkdir(parents=True)
        telemetry = run_directory / "telemetry.mcap"
        telemetry.write_bytes(b"mcap")
        runs.upsert_artifact(run.id, "telemetry", f"runs/{run.id:06d}/telemetry.mcap")
        runs.replace_metrics(run.id, [Metric(name="rms_error_deg", value=0.2, unit="deg")])
        client.app.state.instances.create(build_id=build.id, run_id=run.id)
        runs.transition(run.id, expected=[RunStatus.QUEUED], status=RunStatus.RUNNING)
        runs.transition(run.id, expected=[RunStatus.RUNNING], status=RunStatus.COMPLETED)

        response = client.delete(f"/api/runs/{run.id}")
        assert response.status_code == 204
        assert client.get(f"/api/runs/{run.id}").status_code == 404
        assert not run_directory.exists()
        assert client.app.state.builds.get(build.id).id == build.id
        with client.app.state.database.connect() as connection:
            assert connection.execute("SELECT count(*) FROM metrics WHERE run_id = ?", (run.id,)).fetchone()[0] == 0
            assert connection.execute("SELECT count(*) FROM artifacts WHERE run_id = ?", (run.id,)).fetchone()[0] == 0
            assert connection.execute("SELECT count(*) FROM instances WHERE run_id = ?", (run.id,)).fetchone()[0] == 0

        failed = runs.create(
            commit_sha="b" * 40,
            scenario_name="Failed delete",
            scenario_path="scenario.yaml",
            autopilot="baseline",
        )
        runs.transition(failed.id, expected=[RunStatus.QUEUED], status=RunStatus.FAILED)
        assert client.delete(f"/api/runs/{failed.id}").status_code == 204


def test_delete_run_rejects_active_missing_and_unsafe_paths(settings, tmp_path: Path) -> None:
    with TestClient(create_app(settings, FakeSimulationRunner())) as client:
        runs = client.app.state.repository
        queued = runs.create(
            commit_sha="c" * 40,
            scenario_name="Queued",
            scenario_path="scenario.yaml",
            autopilot="primary",
        )
        queued_directory = settings.runs_dir / f"{queued.id:06d}"
        queued_directory.mkdir(parents=True)
        denied = client.delete(f"/api/runs/{queued.id}")
        assert denied.status_code == 409
        assert queued_directory.is_dir()
        assert client.get(f"/api/runs/{queued.id}").status_code == 200

        running = runs.create(
            commit_sha="d" * 40,
            scenario_name="Running",
            scenario_path="scenario.yaml",
            autopilot="primary",
        )
        runs.transition(running.id, expected=[RunStatus.QUEUED], status=RunStatus.RUNNING)
        assert client.delete(f"/api/runs/{running.id}").status_code == 409
        assert client.delete("/api/runs/999999").status_code == 404

        unsafe = runs.create(
            commit_sha="e" * 40,
            scenario_name="Unsafe",
            scenario_path="scenario.yaml",
            autopilot="primary",
        )
        runs.transition(unsafe.id, expected=[RunStatus.QUEUED], status=RunStatus.FAILED)
        outside = tmp_path / "outside-run-data"
        outside.mkdir()
        (outside / "keep.txt").write_text("keep", encoding="utf-8")
        (settings.runs_dir / f"{unsafe.id:06d}").symlink_to(outside, target_is_directory=True)
        rejected = client.delete(f"/api/runs/{unsafe.id}")
        assert rejected.status_code == 422
        assert (outside / "keep.txt").read_text(encoding="utf-8") == "keep"
        assert client.get(f"/api/runs/{unsafe.id}").status_code == 200


def test_fake_runner_failure_sets_failed_status(settings) -> None:
    with TestClient(create_app(settings, FakeSimulationRunner(exit_code=3))) as client:
        response = client.post(
            "/api/runs",
            json={"scenario": "roll_hold_5deg.yaml", "commit_sha": "abc123"},
        )
        detail = wait_for_terminal(client, response.json()["id"])
        assert detail["run"]["status"] == "failed"
        assert detail["run"]["exit_code"] == 3
        assert "exited with code 3" in detail["run"]["error_message"]
        stages = {stage["id"]: stage for stage in detail["run"]["stages"]}
        assert stages["launch_runner"]["status"] == "failed"
        assert stages["record_telemetry"]["status"] == "skipped"
        assert stages["launch_runner"]["error"] == "runner exited with code 3"


def test_scenario_validation_and_batch_api(settings, tmp_path: Path) -> None:
    runtime = tmp_path / "compatibility-runtime"
    schema_dir = runtime / "contract" / "scenario"
    schema_dir.mkdir(parents=True)
    (schema_dir / "scenario.schema.json").write_text(
        json.dumps(
            {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "required": ["name", "autopilot"],
                "properties": {
                    "name": {"type": "string"},
                    "autopilot": {"enum": ["baseline", "primary"]},
                },
            }
        ),
        encoding="utf-8",
    )

    class Resolver:
        def resolve(self):
            return CompatibilityContract(
                runtime,
                ScenarioRuntime(branch="main", commit="c" * 40),
            )

    with TestClient(create_app(settings, FakeSimulationRunner())) as client:
        client.app.state.scenario_compatibility = Resolver()
        valid = client.post(
            "/api/scenarios/validate",
            json={"yaml": "name: Smoke\nautopilot: baseline\n"},
        )
        assert valid.status_code == 200
        assert valid.json()["valid"] is True
        assert valid.json()["runtime"] == {"branch": "main", "commit": "c" * 40}

        invalid = client.post(
            "/api/scenarios/validate",
            json={"yaml": "name: Bad\nautopilot: unsupported\n"},
        )
        assert invalid.status_code == 200
        assert invalid.json()["valid"] is False
        assert invalid.json()["errors"][0]["path"] == "autopilot"

        batch = client.post(
            "/api/scenarios/validate/batch",
            json={
                "scenarios": [
                    {"id": "valid.yaml", "yaml": "name: OK\nautopilot: primary\n"},
                    {"id": "bad.yaml", "yaml": "name: [unterminated\n"},
                ]
            },
        )
        assert batch.status_code == 200
        assert batch.json()["total"] == 2
        assert batch.json()["valid"] == 1
        assert batch.json()["invalid"] == 1

        malformed = client.post("/api/scenarios/validate", json={})
        assert malformed.status_code == 422

        status = client.get("/api/scenarios/sync/status")
        assert status.json()["configured"] is False
        disabled = client.post("/api/scenarios/sync")
        assert disabled.status_code == 200
        assert disabled.json()["reachable"] is False


def test_managed_scenario_create_api_revalidates_and_never_overwrites(
    settings, tmp_path: Path
) -> None:
    runtime = tmp_path / "managed-runtime"
    schema_dir = runtime / "contract" / "scenario"
    schema_dir.mkdir(parents=True)
    (schema_dir / "scenario.schema.json").write_text(
        json.dumps(
            {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "additionalProperties": False,
                "required": ["schema_version", "scenario_type", "name"],
                "properties": {
                    "schema_version": {"const": 1},
                    "scenario_type": {"const": "roll_hold"},
                    "name": {"type": "string", "minLength": 1},
                },
            }
        ),
        encoding="utf-8",
    )

    class Resolver:
        def resolve(self):
            return CompatibilityContract(
                runtime,
                ScenarioRuntime(branch="main", commit="e" * 40),
            )

    yaml_text = "schema_version: 1\nscenario_type: roll_hold\nname: Created\n"
    with TestClient(create_app(settings, FakeSimulationRunner())) as client:
        resolver = Resolver()
        client.app.state.scenario_compatibility = resolver
        client.app.state.scenario_writer.compatibility = resolver

        created = client.post(
            "/api/scenarios",
            json={"path": "created/roll.yaml", "yaml": yaml_text},
        )
        assert created.status_code == 201
        assert created.json()["source"] == "managed"
        assert created.json()["validation"]["runtime"]["commit"] == "e" * 40
        assert (
            settings.resolved_managed_scenario_dir / "created" / "roll.yaml"
        ).read_text(encoding="utf-8") == yaml_text

        duplicate = client.post(
            "/api/scenarios",
            json={"path": "created/roll.yaml", "yaml": yaml_text},
        )
        assert duplicate.status_code == 409
        traversal = client.post(
            "/api/scenarios",
            json={"path": "../escape.yaml", "yaml": yaml_text},
        )
        assert traversal.status_code == 422
        invalid = client.post(
            "/api/scenarios",
            json={"path": "created/invalid.yaml", "yaml": "name: Invalid\n"},
        )
        assert invalid.status_code == 422
        assert invalid.json()["detail"]["validation"]["valid"] is False
        assert not (
            settings.resolved_managed_scenario_dir / "created" / "invalid.yaml"
        ).exists()

        catalog = client.get("/api/scenario-catalog")
        managed = next(
            item for item in catalog.json() if item["id"] == "managed:created/roll.yaml"
        )
        assert managed["validation"]["valid"] is True
        detail = client.get(
            "/api/scenario-catalog/detail",
            params={"source": "managed", "id": "created/roll.yaml"},
        )
        assert detail.status_code == 200
        assert detail.json()["raw_yaml"] == yaml_text
        assert detail.json()["provenance"]["authority"] == "managed scenario"


def test_scenario_library_catalog_detail_and_path_safety(settings, tmp_path: Path) -> None:
    runtime = tmp_path / "library-runtime"
    schema_dir = runtime / "contract" / "scenario"
    schema_dir.mkdir(parents=True)
    (schema_dir / "scenario.schema.json").write_text(
        json.dumps(
            {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "required": ["schema_version", "scenario_type", "name"],
                "properties": {
                    "schema_version": {"type": "integer"},
                    "scenario_type": {"type": "string"},
                    "name": {"type": "string"},
                },
            }
        ),
        encoding="utf-8",
    )
    original = "# preserved comment\n" + (
        settings.scenario_dir / "roll_hold_5deg.yaml"
    ).read_text(encoding="utf-8")
    (settings.scenario_dir / "roll_hold_5deg.yaml").write_text(
        original, encoding="utf-8"
    )

    class Resolver:
        def resolve(self):
            return CompatibilityContract(
                runtime,
                ScenarioRuntime(branch="main", commit="d" * 40),
            )

    with TestClient(create_app(settings, FakeSimulationRunner())) as client:
        client.app.state.scenario_compatibility = Resolver()
        client.app.state.scenario_catalog.record_invalid(
            source="sftp",
            scenario_id="remote/bad.yaml",
            errors=[
                {
                    "path": "command.roll_deg",
                    "code": "type",
                    "message": "Expected number",
                }
            ],
            validated_commit="d" * 40,
            synced_at="2026-08-30T00:00:00+00:00",
        )
        catalog = client.get("/api/scenario-catalog")
        assert catalog.status_code == 200
        selected = next(
            item for item in catalog.json()
            if item["path"] == "roll_hold_5deg.yaml"
        )
        assert selected["id"] == "bundled:roll_hold_5deg.yaml"
        assert selected["scenario_type"] == "roll_hold"
        assert selected["schema_version"] == 1
        assert selected["validation"] == {
            "valid": True,
            "runtime_branch": "main",
            "runtime_commit": "d" * 40,
            "errors": [],
        }
        rejected = next(item for item in catalog.json() if item["id"] == "sftp:remote/bad.yaml")
        assert rejected["validation"]["valid"] is False
        assert rejected["validation"]["errors"][0]["path"] == "command.roll_deg"

        detail = client.get(
            "/api/scenario-catalog/detail",
            params={"source": "bundled", "id": "roll_hold_5deg.yaml"},
        )
        assert detail.status_code == 200
        assert detail.json()["raw_yaml"] == original
        assert detail.json()["definition"]["command"]["roll_deg"] == 5
        assert detail.json()["provenance"]["integrity"] == "verified"

        invalid_detail = client.get(
            "/api/scenario-catalog/detail",
            params={"source": "sftp", "id": "remote/bad.yaml"},
        )
        assert invalid_detail.status_code == 200
        assert invalid_detail.json()["raw_yaml"] is None
        assert invalid_detail.json()["validation"]["valid"] is False

        traversal = client.get(
            "/api/scenario-catalog/detail",
            params={"source": "bundled", "id": "../secret.yaml"},
        )
        assert traversal.status_code == 404
        assert client.get(
            "/api/scenario-catalog/detail",
            params={"source": "bundled", "id": "missing.yaml"},
        ).status_code == 404


def test_run_scenario_snapshot_is_frozen_and_reports_integrity(settings) -> None:
    source = settings.scenario_dir / "roll_hold_5deg.yaml"
    original = source.read_text(encoding="utf-8")
    with TestClient(create_app(settings, FakeSimulationRunner())) as client:
        created = client.post(
            "/api/runs",
            json={"scenario": "roll_hold_5deg.yaml", "commit_sha": "abc123"},
        )
        assert created.status_code == 202
        run_id = created.json()["id"]
        wait_for_terminal(client, run_id)

        source.write_text(original.replace("roll_deg: 5", "roll_deg: 9"), encoding="utf-8")
        snapshot = client.get(f"/api/runs/{run_id}/scenario")
        assert snapshot.status_code == 200
        assert snapshot.json()["raw_yaml"] == original
        assert snapshot.json()["source"] == "run_snapshot"
        assert snapshot.json()["provenance"]["authority"] == "frozen run snapshot"
        assert snapshot.json()["provenance"]["integrity"] == "verified"

        snapshot_path = settings.runs_dir / f"{run_id:06d}" / "scenario.yaml"
        snapshot_path.write_text(original + "# tampered\n", encoding="utf-8")
        mismatch = client.get(f"/api/runs/{run_id}/scenario")
        assert mismatch.status_code == 200
        assert mismatch.json()["provenance"]["integrity"] == "mismatch"


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
        manifest = settings.runs_dir / f"{response.json()['id']:06d}" / "jsb1-run.json"
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
            },
        )
        assert response.status_code == 503
        assert response.json()["detail"] == "JSB0 Runtime repository is not configured"


def test_branch_run_resolves_revision_reuses_build_and_preserves_history(
    settings, tmp_path: Path, monkeypatch
) -> None:
    runner = FakeSimulationRunner()
    with TestClient(create_app(settings, runner)) as client:
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

        assert client.get("/api/autopilots").status_code == 404
        runtime = client.get("/api/runtime/repository")
        assert runtime.status_code == 200
        assert runtime.json()["id"] == registered.id
        assert runtime.json()["key"] == "jsb0"
        assert {item["name"] for item in client.get("/api/runtime/branches").json()} >= {
            "backend",
            "main",
        }
        missing = client.post(
            "/api/runs",
            json={
                "branch": "missing",
                "scenario": "roll_hold_5deg.yaml",
            },
        )
        assert missing.status_code == 404
        assert missing.json()["detail"] == "Branch no longer exists"

        unsupported_path = settings.scenario_dir / "unsupported.yaml"
        unsupported_path.write_text(
            (settings.scenario_dir / "roll_hold_5deg.yaml")
            .read_text(encoding="utf-8")
            .replace("autopilot: primary", "autopilot: experimental"),
            encoding="utf-8",
        )
        unsupported = client.post(
            "/api/runs",
            json={"branch": "backend", "scenario": unsupported_path.name},
        )
        assert unsupported.status_code == 422
        assert unsupported.json()["detail"] == (
            "Scenario requires unsupported autopilot 'experimental'."
        )

        baseline = client.post(
            "/api/runs",
            json={
                "branch": "backend",
                "scenario": "roll_hold_5deg_baseline.yaml",
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
        assert baseline_detail["run"]["execution_mode"] == "compare"
        assert baseline_detail["run"]["variants"] == ["baseline", "primary"]

        primary = client.post(
            "/api/runs",
            json={
                "branch": "backend",
                "scenario": "roll_hold_5deg.yaml",
            },
        )
        primary_payload = primary.json()
        assert primary_payload["commit_sha"] == backend_v1
        assert primary_payload["build_id"] == baseline_payload["build_id"]
        assert primary_payload["build_reused"] is True
        primary_detail = wait_for_terminal(client, primary_payload["id"])
        assert primary_detail["run"]["execution_mode"] == "compare"
        assert primary_detail["run"]["variants"] == ["baseline", "primary"]
        assert runner.variants_used[:2] == ["baseline", "primary"]

        legacy = client.post(
            "/api/runs",
            json={
                "repository_id": registered.id,
                "branch": "backend",
                "scenario": "roll_hold_5deg_baseline.yaml",
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
                "scenario": "roll_hold_5deg_baseline.yaml",
            },
        )
        advanced_payload = advanced.json()
        assert advanced_payload["commit_sha"] == backend_v2
        assert advanced_payload["build_id"] != baseline_payload["build_id"]
        assert client.get(f"/api/runs/{baseline_payload['id']}").json()["run"]["commit_sha"] == backend_v1
        assert wait_for_terminal(client, advanced_payload["id"])["run"]["status"] == "completed"


def test_roll_hold_controller_parameters_are_validated_frozen_and_delivered(
    settings, tmp_path: Path, monkeypatch
) -> None:
    runner = FakeSimulationRunner()
    with TestClient(create_app(settings, runner)) as client:
        source = settings.resolved_repository_root / "jsb0"
        _, backend_commit = create_jsb0_fixture(source)
        client.app.state.repository_manager.register(
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

        catalog = client.get("/api/runtime/parameters", params={"branch": "backend"})
        assert catalog.status_code == 200
        assert catalog.json()["commit_sha"] == backend_commit
        assert catalog.json()["source"] == "jsb1_px4_roll_hold_adapter"
        assert [item["id"] for item in catalog.json()["parameters"]] == [
            "FW_R_TC",
            "FW_R_RMAX",
            "FW_RR_P",
            "FW_RR_I",
            "FW_RR_D",
            "FW_RR_FF",
            "FW_RR_IMAX",
        ]
        scenario_catalog = client.get("/api/scenarios").json()
        roll_hold = next(
            item
            for item in scenario_catalog
            if item["id"] == "roll_hold_5deg_baseline.yaml"
        )
        assert roll_hold["controller_parameters"] == [
            "FW_R_TC",
            "FW_RR_P",
            "FW_RR_I",
            "FW_RR_D",
            "FW_RR_FF",
            "FW_RR_IMAX",
        ]

        first = client.post(
            "/api/runs",
            json={
                "branch": "backend",
                "scenario": "roll_hold_5deg_baseline.yaml",
                "variant": "baseline",
                "controller_parameters": {"FW_RR_P": 0.08, "FW_RR_D": 0.01},
            },
        )
        assert first.status_code == 202
        detail = wait_for_terminal(client, first.json()["id"])
        assert detail["run"]["controller_parameters"]["FW_RR_P"] == 0.08
        assert detail["run"]["controller_parameters"]["FW_RR_I"] == 0.1
        assert "FW_R_RMAX" not in detail["run"]["controller_parameters"]
        assert detail["run"]["controller_parameter_overrides"] == {
            "FW_RR_D": 0.01,
            "FW_RR_P": 0.08,
        }
        assert runner.parameter_sets_used[-1]["FW_RR_P"] == 0.08
        run_dir = settings.runs_dir / f"{first.json()['id']:06d}"
        assert (run_dir / "parameters.yaml").is_file()
        manifest = json.loads((run_dir / "jsb1-run.json").read_text(encoding="utf-8"))
        assert manifest["controller_parameters"]["FW_RR_D"] == 0.01
        assert manifest["variant_parameters"]["baseline"]["FW_RR_D"] == 0.01

        second = client.post(
            "/api/runs",
            json={
                "branch": "backend",
                "scenario": "roll_hold_5deg_baseline.yaml",
                "variant": "baseline",
                "controller_parameters": {"FW_RR_P": 0.12},
            },
        )
        second_detail = wait_for_terminal(client, second.json()["id"])
        assert second_detail["run"]["scenario_sha256"] == detail["run"]["scenario_sha256"]
        assert second_detail["run"]["controller_parameters"]["FW_RR_P"] == 0.12

        ignored_legacy_variant = client.post(
            "/api/runs",
            json={
                "branch": "backend",
                "scenario": "roll_hold_5deg.yaml",
                "variant": "primary",
                "controller_parameters": {"FW_RR_P": 0.08},
            },
        )
        assert ignored_legacy_variant.status_code == 202
        ignored_detail = wait_for_terminal(client, ignored_legacy_variant.json()["id"])
        assert ignored_detail["run"]["variants"] == ["baseline", "primary"]

        out_of_range = client.post(
            "/api/runs",
            json={
                "branch": "backend",
                "scenario": "roll_hold_5deg_baseline.yaml",
                "variant": "baseline",
                "controller_parameters": {"FW_R_TC": 9.0},
            },
        )
        assert out_of_range.status_code == 422
        assert "FW_R_TC must be <= 1" in out_of_range.json()["detail"]

        not_whitelisted = client.post(
            "/api/runs",
            json={
                "branch": "backend",
                "scenario": "roll_hold_5deg_baseline.yaml",
                "controller_parameters": {"FW_R_RMAX": 60.0},
            },
        )
        assert not_whitelisted.status_code == 422
        assert "Unsupported controller parameters" in not_whitelisted.json()["detail"]

        unsupported_scenario = settings.scenario_dir / "unknown_parameter.yaml"
        unsupported_scenario.write_text(
            (settings.scenario_dir / "roll_hold_5deg_baseline.yaml")
            .read_text(encoding="utf-8")
            .replace("- FW_RR_IMAX\n", "- FW_RR_IMAX\n- UNKNOWN_GAIN\n"),
            encoding="utf-8",
        )
        unsupported = client.post(
            "/api/runs",
            json={"branch": "backend", "scenario": unsupported_scenario.name},
        )
        assert unsupported.status_code == 422
        assert (
            "Unsupported controller parameter for selected JSB0 revision: UNKNOWN_GAIN"
            in unsupported.json()["detail"]
        )


def test_comparison_resolves_once_reuses_one_build_and_tracks_variants(
    settings, tmp_path: Path, monkeypatch
) -> None:
    runner = FakeSimulationRunner()
    with TestClient(create_app(settings, runner)) as client:
        source = settings.resolved_repository_root / "jsb0"
        _, backend_commit = create_jsb0_fixture(source)
        client.app.state.repository_manager.register(
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

        capabilities = client.get("/api/runtime/variants", params={"branch": "backend"})
        assert capabilities.status_code == 200
        assert capabilities.json()["commit_sha"] == backend_commit
        assert capabilities.json()["variants"] == ["baseline", "primary"]

        resolve_calls = 0
        original_resolve = client.app.state.repository_manager.resolve_branch

        def counted_resolve(*args, **kwargs):
            nonlocal resolve_calls
            resolve_calls += 1
            return original_resolve(*args, **kwargs)

        build_calls = 0
        original_build = client.app.state.build_manager.request_resolved

        def counted_build(*args, **kwargs):
            nonlocal build_calls
            build_calls += 1
            return original_build(*args, **kwargs)

        monkeypatch.setattr(client.app.state.repository_manager, "resolve_branch", counted_resolve)
        monkeypatch.setattr(client.app.state.build_manager, "request_resolved", counted_build)

        response = client.post(
            "/api/comparisons",
            json={
                "scenario": "roll_hold_5deg.yaml",
                "branch": "backend",
                "variants": ["baseline", "primary"],
            },
        )
        assert response.status_code == 202
        payload = response.json()
        assert resolve_calls == 1
        assert build_calls == 1
        assert payload["commit_sha"] == backend_commit
        assert [item["execution_variant"] for item in payload["runs"]] == [
            "baseline",
            "primary",
        ]
        run_ids = [item["run_id"] for item in payload["runs"]]
        details = [wait_for_terminal(client, run_id) for run_id in run_ids]
        assert {item["run"]["execution_variant"] for item in details} == {
            "baseline",
            "primary",
        }
        assert len({item["run"]["build_id"] for item in details}) == 1
        assert len({item["run"]["commit_sha"] for item in details}) == 1
        assert len({item["run"]["scenario_sha256"] for item in details}) == 1
        assert all(item["run"]["comparison_id"] == payload["id"] for item in details)
        comparison = client.get(f"/api/comparisons/{payload['id']}")
        assert comparison.json()["status"] == "completed"
        comparison_scenario = client.get(
            f"/api/comparisons/{payload['id']}/scenario"
        )
        assert comparison_scenario.status_code == 200
        assert comparison_scenario.json()["source"] == "run_snapshot"
        assert comparison_scenario.json()["provenance"]["authority"] == (
            "frozen comparison snapshot"
        )
        assert comparison_scenario.json()["provenance"]["integrity"] == "verified"
        snapshot_bytes = Path(comparison.json()["scenario_path"]).read_bytes()
        assert all(Path(item["run"]["scenario_path"]).read_bytes() == snapshot_bytes for item in details)
        assert runner.variants_used[-2:] == ["baseline", "primary"]

        duplicate = client.post(
            "/api/comparisons",
            json={
                "scenario": "roll_hold_5deg.yaml",
                "branch": "backend",
                "variants": ["baseline", "baseline"],
            },
        )
        assert duplicate.status_code == 422

        unsupported = client.post(
            "/api/comparisons",
            json={
                "scenario": "roll_hold_5deg.yaml",
                "branch": "backend",
                "variants": ["baseline", "experimental"],
            },
        )
        assert unsupported.status_code == 422
        assert "Unsupported execution variant" in unsupported.json()["detail"]
        assert len(client.get("/api/comparisons").json()) == 1
