from __future__ import annotations

import sqlite3
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import pytest
from app.domain.artifacts import StoredArtifact
from app.domain.build import BuildStatus
from app.domain.errors import InvalidStatusTransition
from app.domain.execution import FrozenRunPreparation
from app.domain.models import RunStatus
from app.repositories.builds import BuildRepository
from app.repositories.comparisons import ComparisonRepository
from app.repositories.database import Database
from app.repositories.jsb_repository_repository import JsbRepositoryRepository
from app.repositories.runs import RunRepository
from app.services.run_deletion import RunDeletionService


def persistence(tmp_path: Path) -> tuple[Database, RunRepository, BuildRepository]:
    migrations = Path(__file__).resolve().parents[1] / "migrations"
    database = Database(tmp_path / "data" / "test.db", migrations)
    database.initialize()
    return database, RunRepository(database), BuildRepository(database)


def create_run(runs: RunRepository):
    return runs.create(
        commit_sha="a" * 40,
        scenario_name="roll.yaml",
        scenario_path="pending",
        autopilot="primary",
        contract_version="2.1.0",
    )


def prepare_run(runs: RunRepository, run_id: int) -> None:
    runs.finalize_preparation(
        run_id,
        scenario_path=f"runs/{run_id:06d}/inputs/scenario.yaml",
        scenario_sha256="b" * 64,
        output_directory=f"runs/{run_id:06d}",
        parameter_snapshot_path=f"runs/{run_id:06d}/inputs/parameters.yaml",
        parameter_snapshot_sha256="c" * 64,
        artifacts=[
            StoredArtifact(
                "scenario",
                f"runs/{run_id:06d}/inputs/scenario.yaml",
                "b" * 64,
                128,
            ),
            StoredArtifact(
                "parameters",
                f"runs/{run_id:06d}/inputs/parameters.yaml",
                "c" * 64,
                64,
            ),
        ],
    )


def test_domain_and_database_reject_invalid_lifecycle_edges(tmp_path: Path) -> None:
    database, runs, builds = persistence(tmp_path)
    run = create_run(runs)

    with pytest.raises(InvalidStatusTransition, match="queued to completed"):
        runs.transition(
            run.id,
            expected=[RunStatus.QUEUED],
            status=RunStatus.COMPLETED,
        )
    with (
        database.connect() as connection,
        pytest.raises(sqlite3.IntegrityError, match="invalid run status transition"),
    ):
        connection.execute(
            "UPDATE runs SET status = 'completed' WHERE id = ?", (run.id,)
        )

    repository = JsbRepositoryRepository(database).create(
        name="jsb0",
        remote_url="local",
        local_path="jsb0",
        default_branch="impl",
    )
    build = builds.create(
        repository_id=repository.id,
        commit_sha="d" * 40,
        branch="impl",
        build_dir="000001",
        stdout_path="000001/stdout.log",
        stderr_path="000001/stderr.log",
    )
    with pytest.raises(InvalidStatusTransition, match="queued to completed"):
        builds.transition(
            build.id,
            expected=[BuildStatus.QUEUED],
            status=BuildStatus.COMPLETED,
        )


def test_concurrent_run_claim_has_exactly_one_winner(tmp_path: Path) -> None:
    _, runs, _ = persistence(tmp_path)
    run = create_run(runs)
    prepare_run(runs, run.id)

    def claim(index: int) -> bool:
        return (
            runs.claim_for_execution(
                run.id, started_at=f"2026-09-04T00:00:{index:02d}Z"
            )
            is not None
        )

    with ThreadPoolExecutor(max_workers=8) as executor:
        results = list(executor.map(claim, range(8)))

    assert results.count(True) == 1
    assert runs.get(run.id).status is RunStatus.RUNNING


def test_prepared_run_provenance_is_immutable_and_artifacts_are_metadata_only(
    tmp_path: Path,
) -> None:
    database, runs, _ = persistence(tmp_path)
    run = create_run(runs)
    prepare_run(runs, run.id)
    frozen = runs.get(run.id)

    assert frozen.scenario_path == f"runs/{run.id:06d}/inputs/scenario.yaml"
    assert frozen.parameter_snapshot_sha256 == "c" * 64
    assert frozen.contract_version == "2.1.0"
    with (
        database.connect() as connection,
        pytest.raises(sqlite3.IntegrityError, match="provenance is immutable"),
    ):
        connection.execute(
            "UPDATE runs SET commit_sha = ? WHERE id = ?", ("f" * 40, run.id)
        )

    artifact = runs.get_artifact_row(run.id, "scenario")
    assert artifact == {
        "id": artifact["id"],
        "run_id": run.id,
        "kind": "scenario",
        "path": f"runs/{run.id:06d}/inputs/scenario.yaml",
        "sha256": "b" * 64,
        "size_bytes": 128,
    }
    with pytest.raises(ValueError, match="safe relative path"):
        StoredArtifact("telemetry", "/host/private/flight.mcap", "a" * 64, 1)
    with (
        database.connect() as connection,
        pytest.raises(sqlite3.IntegrityError, match="invalid artifact metadata"),
    ):
        connection.execute(
            """INSERT INTO artifacts(run_id, kind, path, sha256, size_bytes)
               VALUES (?, 'telemetry', '/host/private/flight.mcap', ?, 1)""",
            (run.id, "a" * 64),
        )


def test_build_key_registry_and_active_index_prevent_duplicate_reservations(
    tmp_path: Path,
) -> None:
    database, _, builds = persistence(tmp_path)
    repository = JsbRepositoryRepository(database).create(
        name="jsb0",
        remote_url="local",
        local_path="jsb0",
        default_branch="impl",
    )

    def reserve(_index: int) -> int:
        build, _ = builds.reserve(
            repository_id=repository.id,
            commit_sha="e" * 40,
            branch="impl",
            rebuild=False,
            paths_for_id=lambda build_id: (
                f"{build_id:06d}",
                f"{build_id:06d}/stdout.log",
                f"{build_id:06d}/stderr.log",
            ),
        )
        return build.id

    with ThreadPoolExecutor(max_workers=8) as executor:
        build_ids = list(executor.map(reserve, range(8)))

    assert len(set(build_ids)) == 1
    with database.connect() as connection:
        key = connection.execute(
            """SELECT build_id FROM build_keys
               WHERE repository_id = ? AND commit_sha = ?""",
            (repository.id, "e" * 40),
        ).fetchone()
        assert key["build_id"] == build_ids[0]
        with pytest.raises(sqlite3.IntegrityError):
            connection.execute(
                """INSERT INTO builds
                   (repository_id, commit_sha, branch, status, build_dir,
                    stdout_path, stderr_path, created_at)
                   VALUES (?, ?, 'impl', 'queued', 'duplicate',
                           'duplicate/out', 'duplicate/err', 'now')""",
                (repository.id, "e" * 40),
            )


def test_comparison_creation_rolls_back_all_rows_on_child_conflict(
    tmp_path: Path,
) -> None:
    database, _, builds = persistence(tmp_path)
    repository = JsbRepositoryRepository(database).create(
        name="jsb0",
        remote_url="local",
        local_path="jsb0",
        default_branch="impl",
    )
    build = builds.create(
        repository_id=repository.id,
        commit_sha="f" * 40,
        branch="impl",
        build_dir="000001",
        stdout_path="000001/stdout.log",
        stderr_path="000001/stderr.log",
    )
    comparisons = ComparisonRepository(database)

    with pytest.raises(sqlite3.IntegrityError):
        comparisons.create_with_runs(
            scenario_id="roll.yaml",
            scenario_source="bundled",
            scenario_name="Roll",
            scenario_type="roll_hold",
            scenario_sha256="a" * 64,
            scenario_path="pending",
            repository_id=repository.id,
            branch="impl",
            commit_sha="f" * 40,
            build_id=build.id,
            variants=("primary", "primary"),
        )
    with database.connect() as connection:
        assert connection.execute("SELECT count(*) FROM comparisons").fetchone()[0] == 0
        assert connection.execute("SELECT count(*) FROM runs").fetchone()[0] == 0


def test_comparison_preparation_publish_is_all_or_nothing(tmp_path: Path) -> None:
    database, runs, builds = persistence(tmp_path)
    repository = JsbRepositoryRepository(database).create(
        name="jsb0",
        remote_url="local",
        local_path="jsb0",
        default_branch="impl",
    )
    build = builds.create(
        repository_id=repository.id,
        commit_sha="0" * 40,
        branch="impl",
        build_dir="000001",
        stdout_path="000001/stdout.log",
        stderr_path="000001/stderr.log",
    )
    comparisons = ComparisonRepository(database)
    comparison, run_ids = comparisons.create_with_runs(
        scenario_id="roll.yaml",
        scenario_source="bundled",
        scenario_name="Roll",
        scenario_type="roll_hold",
        scenario_sha256="a" * 64,
        scenario_path="pending",
        repository_id=repository.id,
        branch="impl",
        commit_sha="0" * 40,
        build_id=build.id,
        variants=("baseline", "primary"),
    )
    valid = FrozenRunPreparation(
        run_id=run_ids[0],
        scenario_path="runs/000001/scenario.yaml",
        scenario_sha256="a" * 64,
        output_directory="runs/000001",
        parameter_snapshot_path=None,
        parameter_snapshot_sha256=None,
        artifacts=(
            StoredArtifact("scenario", "runs/000001/scenario.yaml", "a" * 64, 10),
        ),
    )
    invalid = FrozenRunPreparation(
        run_id=999999,
        scenario_path="runs/999999/scenario.yaml",
        scenario_sha256="a" * 64,
        output_directory="runs/999999",
        parameter_snapshot_path=None,
        parameter_snapshot_sha256=None,
        artifacts=(
            StoredArtifact("scenario", "runs/999999/scenario.yaml", "a" * 64, 10),
        ),
    )

    with pytest.raises(InvalidStatusTransition):
        comparisons.finalize_preparation(
            comparison.id,
            scenario_path="comparisons/000001/scenario.yaml",
            build_id=build.id,
            runs=(valid, invalid),
        )

    assert runs.get(run_ids[0]).output_directory is None
    assert runs.get_artifact_rows(run_ids[0]) == []
    with database.connect() as connection:
        assert connection.execute("SELECT count(*) FROM instances").fetchone()[0] == 0
        assert (
            connection.execute(
                "SELECT scenario_path FROM comparisons WHERE id = ?", (comparison.id,)
            ).fetchone()["scenario_path"]
            == "pending"
        )


def test_run_deletion_restores_staged_directory_when_database_fails(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    _, runs, _ = persistence(tmp_path)
    run = create_run(runs)
    runs.fail_run(
        run.id,
        finished_at="2026-09-04T00:00:00Z",
        error_message="failed",
    )
    run_directory = tmp_path / "data" / "runs" / f"{run.id:06d}"
    run_directory.mkdir(parents=True)
    (run_directory / "keep.txt").write_text("keep", encoding="utf-8")

    def fail_delete(_run_id: int):
        raise sqlite3.OperationalError("database unavailable")

    monkeypatch.setattr(runs, "delete_terminal", fail_delete)
    with pytest.raises(sqlite3.OperationalError):
        RunDeletionService(runs, tmp_path / "data" / "runs").delete(run.id)

    assert (run_directory / "keep.txt").read_text(encoding="utf-8") == "keep"
    assert runs.get(run.id).status is RunStatus.FAILED


def test_worker_recovery_fails_only_claimed_rows(tmp_path: Path) -> None:
    database, runs, builds = persistence(tmp_path)
    queued_run = create_run(runs)
    running_run = create_run(runs)
    prepare_run(runs, queued_run.id)
    prepare_run(runs, running_run.id)
    assert runs.claim_for_execution(running_run.id, started_at="2026-09-04T00:00:00Z")

    repository = JsbRepositoryRepository(database).create(
        name="jsb0",
        remote_url="local",
        local_path="jsb0",
        default_branch="impl",
    )
    queued_build = builds.create(
        repository_id=repository.id,
        commit_sha="1" * 40,
        branch="impl",
        build_dir="000001",
        stdout_path="000001/stdout.log",
        stderr_path="000001/stderr.log",
    )
    running_build = builds.create(
        repository_id=repository.id,
        commit_sha="2" * 40,
        branch="impl",
        build_dir="000002",
        stdout_path="000002/stdout.log",
        stderr_path="000002/stderr.log",
    )
    assert builds.claim_for_build(running_build.id, started_at="2026-09-04T00:00:00Z")

    assert runs.fail_running_from_previous_worker() == 1
    assert builds.fail_running_from_previous_worker() == 1
    assert runs.get(queued_run.id).status is RunStatus.QUEUED
    assert runs.get(running_run.id).status is RunStatus.FAILED
    assert builds.get(queued_build.id).status is BuildStatus.QUEUED
    assert builds.get(running_build.id).status is BuildStatus.FAILED


def test_last_comparison_run_deletion_removes_orphaned_comparison(
    tmp_path: Path,
) -> None:
    database, runs, builds = persistence(tmp_path)
    repository = JsbRepositoryRepository(database).create(
        name="jsb0",
        remote_url="local",
        local_path="jsb0",
        default_branch="impl",
    )
    build = builds.create(
        repository_id=repository.id,
        commit_sha="3" * 40,
        branch="impl",
        build_dir="000001",
        stdout_path="000001/stdout.log",
        stderr_path="000001/stderr.log",
    )
    comparisons = ComparisonRepository(database)
    comparison, run_ids = comparisons.create_with_runs(
        scenario_id="roll.yaml",
        scenario_source="bundled",
        scenario_name="Roll",
        scenario_type="roll_hold",
        scenario_sha256="a" * 64,
        scenario_path="comparisons/000001/scenario.yaml",
        repository_id=repository.id,
        branch="impl",
        commit_sha="3" * 40,
        build_id=build.id,
        variants=("baseline", "primary"),
    )
    for run_id in run_ids:
        runs.fail_run(
            run_id,
            finished_at="2026-09-04T00:00:00Z",
            error_message="failed",
        )
    comparison_directory = tmp_path / "data" / "comparisons" / f"{comparison.id:06d}"
    comparison_directory.mkdir(parents=True)
    (comparison_directory / "scenario.yaml").write_text(
        "name: Roll\n", encoding="utf-8"
    )
    deletion = RunDeletionService(runs, tmp_path / "data" / "runs")

    deletion.delete(run_ids[0])
    assert comparisons.get(comparison.id).id == comparison.id
    assert comparison_directory.is_dir()

    deletion.delete(run_ids[1])
    with pytest.raises(KeyError):
        comparisons.get(comparison.id)
    assert not comparison_directory.exists()


def test_failed_migration_rolls_back_ddl_and_version_marker(tmp_path: Path) -> None:
    migrations = tmp_path / "migrations"
    migrations.mkdir()
    (migrations / "001_broken.sql").write_text(
        "CREATE TABLE should_rollback(id INTEGER);\nTHIS IS NOT SQL;\n",
        encoding="utf-8",
    )
    database = Database(tmp_path / "broken.db", migrations)

    with pytest.raises(sqlite3.OperationalError):
        database.initialize()

    with database.connect() as connection:
        assert (
            connection.execute(
                """SELECT count(*) FROM sqlite_master
               WHERE type = 'table' AND name = 'should_rollback'"""
            ).fetchone()[0]
            == 0
        )
        assert (
            connection.execute(
                "SELECT count(*) FROM schema_migrations WHERE version = '001_broken'"
            ).fetchone()[0]
            == 0
        )
