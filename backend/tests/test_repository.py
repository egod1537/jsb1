from pathlib import Path

import pytest
from app.domain.artifacts import StoredArtifact
from app.domain.models import Metric, RunStatus
from app.repositories.database import Database
from app.repositories.runs import InvalidStatusTransition, RunRepository


def repository(tmp_path: Path) -> RunRepository:
    migrations = Path(__file__).resolve().parents[1] / "migrations"
    database = Database(tmp_path / "test.db", migrations)
    database.initialize()
    return RunRepository(database)


def test_run_crud_and_status_transitions(tmp_path: Path) -> None:
    repo = repository(tmp_path)
    run = repo.create(
        commit_sha="abc123",
        scenario_name="roll.yaml",
        scenario_path="/scenarios/roll.yaml",
        autopilot="primary",
    )
    assert run.status is RunStatus.QUEUED
    repo.transition(run.id, expected=[RunStatus.QUEUED], status=RunStatus.RUNNING)
    completed = repo.transition(
        run.id, expected=[RunStatus.RUNNING], status=RunStatus.COMPLETED, exit_code=0
    )
    assert completed.exit_code == 0
    assert repo.list(status=RunStatus.COMPLETED)[0].id == run.id
    with pytest.raises(InvalidStatusTransition):
        repo.transition(run.id, expected=[RunStatus.QUEUED], status=RunStatus.RUNNING)


def test_metrics_and_artifacts(tmp_path: Path) -> None:
    repo = repository(tmp_path)
    run = repo.create(
        commit_sha="abc123",
        scenario_name="roll.yaml",
        scenario_path="roll.yaml",
        autopilot="primary",
    )
    repo.replace_metrics(run.id, [Metric(name="rms_error_deg", value=0.25, unit="deg")])
    repo.record_artifact(
        run.id,
        StoredArtifact("telemetry", "runs/000001/telemetry.mcap", "a" * 64, 4),
    )
    assert repo.get_metrics(run.id)[0].value == 0.25
    assert repo.get_artifact_row(run.id, "telemetry")["path"].endswith("telemetry.mcap")


def test_run_claim_is_atomic_and_idempotent(tmp_path: Path) -> None:
    repo = repository(tmp_path)
    run = repo.create(
        commit_sha="a" * 40,
        scenario_name="roll.yaml",
        scenario_path="roll.yaml",
        autopilot="primary",
    )

    assert repo.claim_for_execution(run.id, started_at="2026-09-04T00:00:00Z") is None
    repo.finalize_preparation(
        run.id,
        scenario_path="runs/000001/scenario.yaml",
        scenario_sha256="b" * 64,
        output_directory="runs/000001",
        parameter_snapshot_path=None,
        parameter_snapshot_sha256=None,
        artifacts=[
            StoredArtifact("scenario", "runs/000001/scenario.yaml", "b" * 64, 10)
        ],
    )
    claimed = repo.claim_for_execution(run.id, started_at="2026-09-04T00:00:01Z")

    assert claimed is not None
    assert claimed.status is RunStatus.RUNNING
    assert repo.claim_for_execution(run.id, started_at="2026-09-04T00:00:02Z") is None
