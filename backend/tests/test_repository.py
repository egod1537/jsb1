from pathlib import Path

import pytest

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
    repo.set_output_directory(run.id, "runs/000001")
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
        commit_sha="abc123", scenario_name="roll.yaml", scenario_path="roll.yaml", autopilot="primary"
    )
    repo.replace_metrics(run.id, [Metric(name="rms_error_deg", value=0.25, unit="deg")])
    repo.upsert_artifact(run.id, "telemetry", "runs/000001/telemetry.mcap")
    assert repo.get_metrics(run.id)[0].value == 0.25
    assert repo.get_artifact_row(run.id, "telemetry")["path"].endswith("telemetry.mcap")

