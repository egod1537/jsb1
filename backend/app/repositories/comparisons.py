from __future__ import annotations

from collections.abc import Sequence

from app.domain.clock import utc_now
from app.domain.errors import InvalidStatusTransition
from app.domain.execution import FrozenRunPreparation
from app.domain.models import (
    Comparison,
    ComparisonRun,
    ComparisonStatus,
    RunStatus,
)
from app.repositories.database import Database


class ComparisonRepository:
    def __init__(self, database: Database) -> None:
        self.database = database

    def create_with_runs(
        self,
        *,
        scenario_id: str,
        scenario_source: str,
        scenario_name: str,
        scenario_type: str | None,
        scenario_sha256: str,
        scenario_path: str,
        repository_id: int,
        branch: str,
        commit_sha: str,
        build_id: int,
        variants: Sequence[str],
        contract_version: str | None = None,
    ) -> tuple[Comparison, list[int]]:
        created_at = utc_now()
        with self.database.connect() as connection:
            cursor = connection.execute(
                """INSERT INTO comparisons
                   (scenario_id, scenario_source, scenario_name, scenario_type,
                    scenario_sha256, scenario_path, repository_id, branch,
                    commit_sha, build_id, created_at)
                   VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                (
                    scenario_id,
                    scenario_source,
                    scenario_name,
                    scenario_type,
                    scenario_sha256,
                    scenario_path,
                    repository_id,
                    branch,
                    commit_sha,
                    build_id,
                    created_at,
                ),
            )
            comparison_id = int(cursor.lastrowid)
            run_ids: list[int] = []
            for variant in variants:
                run_cursor = connection.execute(
                    """INSERT INTO runs
                       (status, repository_id, branch, build_id, commit_sha,
                        scenario_name, scenario_type, scenario_path, autopilot,
                        execution_variant, comparison_id, scenario_id,
                        scenario_source, scenario_sha256, contract_version, created_at)
                       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                    (
                        RunStatus.QUEUED.value,
                        repository_id,
                        branch,
                        build_id,
                        commit_sha,
                        scenario_name,
                        scenario_type,
                        scenario_path,
                        variant,
                        variant,
                        comparison_id,
                        scenario_id,
                        scenario_source,
                        scenario_sha256,
                        contract_version,
                        created_at,
                    ),
                )
                run_ids.append(int(run_cursor.lastrowid))
        return self.get(comparison_id), run_ids

    def get(self, comparison_id: int) -> Comparison:
        with self.database.connect() as connection:
            row = connection.execute(
                "SELECT * FROM comparisons WHERE id = ?", (comparison_id,)
            ).fetchone()
            run_rows = connection.execute(
                """SELECT id AS run_id, execution_variant, status,
                          error_message, wall_time_sec
                   FROM runs WHERE comparison_id = ? ORDER BY id""",
                (comparison_id,),
            ).fetchall()
        if row is None:
            raise KeyError(comparison_id)
        runs = [ComparisonRun.model_validate(dict(item)) for item in run_rows]
        return Comparison.model_validate(
            {**dict(row), "status": self.aggregate_status(runs), "runs": runs}
        )

    def list(self, limit: int = 50) -> list[Comparison]:
        with self.database.connect() as connection:
            ids = [
                int(row["id"])
                for row in connection.execute(
                    "SELECT id FROM comparisons ORDER BY id DESC LIMIT ?", (limit,)
                ).fetchall()
            ]
        return [self.get(comparison_id) for comparison_id in ids]

    def finalize_preparation(
        self,
        comparison_id: int,
        *,
        scenario_path: str,
        build_id: int,
        runs: Sequence[FrozenRunPreparation],
    ) -> Comparison:
        """Atomically publish every frozen child Run and its Instance."""
        with self.database.connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            comparison = connection.execute(
                "SELECT id FROM comparisons WHERE id = ? AND build_id = ?",
                (comparison_id, build_id),
            ).fetchone()
            if comparison is None:
                raise KeyError(comparison_id)
            connection.execute(
                "UPDATE comparisons SET scenario_path = ? WHERE id = ?",
                (scenario_path, comparison_id),
            )
            for preparation in runs:
                cursor = connection.execute(
                    """UPDATE runs
                       SET scenario_path = ?, scenario_sha256 = ?,
                           parameter_snapshot_path = ?,
                           parameter_snapshot_sha256 = ?, output_directory = ?
                       WHERE id = ? AND comparison_id = ? AND build_id = ?
                         AND status = 'queued' AND output_directory IS NULL""",
                    (
                        preparation.scenario_path,
                        preparation.scenario_sha256,
                        preparation.parameter_snapshot_path,
                        preparation.parameter_snapshot_sha256,
                        preparation.output_directory,
                        preparation.run_id,
                        comparison_id,
                        build_id,
                    ),
                )
                if cursor.rowcount != 1:
                    raise InvalidStatusTransition(
                        f"run {preparation.run_id} cannot finalize comparison preparation"
                    )
                connection.executemany(
                    """INSERT INTO artifacts
                       (run_id, kind, path, sha256, size_bytes)
                       VALUES (?, ?, ?, ?, ?)
                       ON CONFLICT(run_id, kind) DO UPDATE SET
                         path = excluded.path,
                         sha256 = excluded.sha256,
                         size_bytes = excluded.size_bytes""",
                    [
                        (
                            preparation.run_id,
                            artifact.kind,
                            artifact.relative_path,
                            artifact.sha256,
                            artifact.size_bytes,
                        )
                        for artifact in preparation.artifacts
                    ],
                )
                connection.execute(
                    """INSERT INTO instances(build_id, run_id, status)
                       VALUES (?, ?, 'queued')""",
                    (build_id, preparation.run_id),
                )
        return self.get(comparison_id)

    @staticmethod
    def aggregate_status(runs: Sequence[ComparisonRun]) -> ComparisonStatus:
        statuses = [run.status for run in runs]
        if statuses and all(status is RunStatus.COMPLETED for status in statuses):
            return ComparisonStatus.COMPLETED
        if statuses and all(status is RunStatus.FAILED for status in statuses):
            return ComparisonStatus.FAILED
        if any(status in {RunStatus.QUEUED, RunStatus.RUNNING} for status in statuses):
            return (
                ComparisonStatus.RUNNING
                if any(status is RunStatus.RUNNING for status in statuses)
                or any(status is RunStatus.FAILED for status in statuses)
                else ComparisonStatus.QUEUED
            )
        return ComparisonStatus.PARTIAL_FAILED
