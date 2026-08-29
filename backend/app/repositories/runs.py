from __future__ import annotations

from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

from app.domain.models import Metric, Run, RunStatus, RunSummary
from app.repositories.database import Database


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


class InvalidStatusTransition(RuntimeError):
    pass


class RunRepository:
    def __init__(self, database: Database) -> None:
        self.database = database

    def create(
        self,
        *,
        commit_sha: str | None,
        repository_id: int | None = None,
        build_id: int | None = None,
        scenario_name: str,
        scenario_path: str,
        autopilot: str,
    ) -> Run:
        with self.database.connect() as connection:
            cursor = connection.execute(
                """INSERT INTO runs
                   (status, repository_id, build_id, commit_sha, scenario_name,
                    scenario_path, autopilot, created_at)
                   VALUES (?, ?, ?, ?, ?, ?, ?, ?)""",
                (
                    RunStatus.QUEUED.value,
                    repository_id,
                    build_id,
                    commit_sha,
                    scenario_name,
                    scenario_path,
                    autopilot,
                    utc_now(),
                ),
            )
            run_id = int(cursor.lastrowid)
        return self.get(run_id)

    def fail_incomplete_from_previous_process(self) -> int:
        """An in-memory queue cannot resume work after a process restart."""
        with self.database.connect() as connection:
            cursor = connection.execute(
                """UPDATE runs
                   SET status = 'failed', finished_at = ?,
                       error_message = 'backend restarted before run completed'
                   WHERE status IN ('queued', 'running')""",
                (utc_now(),),
            )
            return cursor.rowcount

    def get(self, run_id: int) -> Run:
        with self.database.connect() as connection:
            row = connection.execute(
                """SELECT runs.*, repositories.name AS repository_name,
                          builds.branch AS build_branch
                   FROM runs
                   LEFT JOIN repositories ON repositories.id = runs.repository_id
                   LEFT JOIN builds ON builds.id = runs.build_id
                   WHERE runs.id = ?""",
                (run_id,),
            ).fetchone()
        if row is None:
            raise KeyError(run_id)
        return Run.model_validate(dict(row))

    def list(
        self,
        *,
        status: RunStatus | None = None,
        scenario: str | None = None,
        limit: int = 50,
    ) -> list[RunSummary]:
        clauses: list[str] = []
        parameters: list[Any] = []
        if status is not None:
            clauses.append("runs.status = ?")
            parameters.append(status.value)
        if scenario:
            clauses.append("runs.scenario_name = ?")
            parameters.append(scenario)
        where = f"WHERE {' AND '.join(clauses)}" if clauses else ""
        parameters.append(limit)
        with self.database.connect() as connection:
            rows = connection.execute(
                f"""SELECT runs.id, runs.status, runs.repository_id,
                           repositories.name AS repository_name, runs.build_id,
                           builds.branch AS build_branch, runs.commit_sha,
                           runs.scenario_name, runs.autopilot, runs.created_at,
                           runs.wall_time_sec
                    FROM runs
                    LEFT JOIN repositories ON repositories.id = runs.repository_id
                    LEFT JOIN builds ON builds.id = runs.build_id
                    {where} ORDER BY runs.id DESC LIMIT ?""",  # noqa: S608
                parameters,
            ).fetchall()
        return [RunSummary.model_validate(dict(row)) for row in rows]

    def set_output_directory(self, run_id: int, relative_path: str) -> None:
        with self.database.connect() as connection:
            connection.execute(
                "UPDATE runs SET output_directory = ? WHERE id = ?",
                (relative_path, run_id),
            )

    def transition(
        self,
        run_id: int,
        *,
        expected: Iterable[RunStatus],
        status: RunStatus,
        **fields: Any,
    ) -> Run:
        values = {"status": status.value, **fields}
        assignments = ", ".join(f"{name} = ?" for name in values)
        expected_values = [item.value for item in expected]
        placeholders = ", ".join("?" for _ in expected_values)
        with self.database.connect() as connection:
            cursor = connection.execute(
                f"UPDATE runs SET {assignments} WHERE id = ? AND status IN ({placeholders})",  # noqa: S608
                [*values.values(), run_id, *expected_values],
            )
            if cursor.rowcount != 1:
                raise InvalidStatusTransition(f"invalid transition for run {run_id} to {status}")
        return self.get(run_id)

    def replace_metrics(self, run_id: int, metrics: Iterable[Metric]) -> None:
        with self.database.connect() as connection:
            connection.execute("DELETE FROM metrics WHERE run_id = ?", (run_id,))
            connection.executemany(
                "INSERT INTO metrics(run_id, name, value, unit) VALUES (?, ?, ?, ?)",
                [(run_id, metric.name, metric.value, metric.unit) for metric in metrics],
            )

    def get_metrics(self, run_id: int) -> list[Metric]:
        with self.database.connect() as connection:
            rows = connection.execute(
                "SELECT name, value, unit FROM metrics WHERE run_id = ? ORDER BY id",
                (run_id,),
            ).fetchall()
        return [Metric.model_validate(dict(row)) for row in rows]

    def upsert_artifact(self, run_id: int, kind: str, relative_path: str) -> None:
        with self.database.connect() as connection:
            connection.execute(
                """INSERT INTO artifacts(run_id, kind, path) VALUES (?, ?, ?)
                   ON CONFLICT(run_id, kind) DO UPDATE SET path = excluded.path""",
                (run_id, kind, relative_path),
            )

    def get_artifact_rows(self, run_id: int) -> list[dict[str, Any]]:
        with self.database.connect() as connection:
            rows = connection.execute(
                "SELECT id, run_id, kind, path FROM artifacts WHERE run_id = ? ORDER BY id",
                (run_id,),
            ).fetchall()
        return [dict(row) for row in rows]

    def get_artifact_row(self, run_id: int, kind: str) -> dict[str, Any]:
        with self.database.connect() as connection:
            row = connection.execute(
                "SELECT id, run_id, kind, path FROM artifacts WHERE run_id = ? AND kind = ?",
                (run_id, kind),
            ).fetchone()
        if row is None:
            raise KeyError((run_id, kind))
        return dict(row)
