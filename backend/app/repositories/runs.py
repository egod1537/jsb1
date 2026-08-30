from __future__ import annotations

from collections.abc import Iterable
from datetime import datetime, timezone
import json
from typing import Any

from app.domain.models import Metric, Run, RunStatus, RunSummary
from app.domain.pipeline import PipelineStage
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
        branch: str | None = None,
        build_id: int | None = None,
        scenario_name: str,
        scenario_path: str,
        autopilot: str,
        execution_variant: str | None = None,
        comparison_id: int | None = None,
        scenario_type: str | None = None,
        scenario_id: str | None = None,
        scenario_source: str | None = None,
        scenario_sha256: str | None = None,
        controller_parameters: dict[str, float] | None = None,
        controller_parameter_overrides: dict[str, float] | None = None,
        execution_mode: str = "single",
        variants: list[str] | None = None,
        variant_parameters: dict[str, dict[str, float]] | None = None,
    ) -> Run:
        with self.database.connect() as connection:
            cursor = connection.execute(
                """INSERT INTO runs
                   (status, repository_id, branch, build_id, commit_sha, scenario_name,
                    scenario_type, scenario_path, autopilot, execution_variant,
                    comparison_id, scenario_id, scenario_source, scenario_sha256,
                    controller_parameters, controller_parameter_overrides,
                    execution_mode, variants, variant_parameters, created_at)
                   VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                (
                    RunStatus.QUEUED.value,
                    repository_id,
                    branch,
                    build_id,
                    commit_sha,
                    scenario_name,
                    scenario_type,
                    scenario_path,
                    autopilot,
                    execution_variant or autopilot,
                    comparison_id,
                    scenario_id,
                    scenario_source,
                    scenario_sha256,
                    json.dumps(controller_parameters or {}, separators=(",", ":")),
                    json.dumps(controller_parameter_overrides or {}, separators=(",", ":")),
                    execution_mode,
                    json.dumps(variants or [execution_variant or autopilot], separators=(",", ":")),
                    json.dumps(variant_parameters or {}, separators=(",", ":")),
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

    def ids_with_statuses(self, statuses: Iterable[RunStatus]) -> list[int]:
        values = [status.value for status in statuses]
        if not values:
            return []
        placeholders = ", ".join("?" for _ in values)
        with self.database.connect() as connection:
            rows = connection.execute(
                f"SELECT id FROM runs WHERE status IN ({placeholders}) ORDER BY id",  # noqa: S608
                values,
            ).fetchall()
        return [int(row["id"]) for row in rows]

    def fail_running_from_previous_worker(self) -> int:
        """Fail only work that was owned by a vanished worker.

        Queued rows are the durable queue and intentionally survive API and
        worker restarts.
        """
        with self.database.connect() as connection:
            cursor = connection.execute(
                """UPDATE runs
                   SET status = 'failed', finished_at = ?,
                       error_message = 'execution worker restarted during run'
                   WHERE status = 'running'""",
                (utc_now(),),
            )
            return cursor.rowcount

    def queued_candidates(self, *, limit: int = 200) -> list[tuple[int, str | None]]:
        with self.database.connect() as connection:
            rows = connection.execute(
                """SELECT runs.id, builds.status AS build_status
                   FROM runs
                   LEFT JOIN builds ON builds.id = runs.build_id
                   WHERE runs.status = 'queued'
                   ORDER BY runs.id
                   LIMIT ?""",
                (limit,),
            ).fetchall()
        return [
            (int(row["id"]), row["build_status"])
            for row in rows
        ]

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
        return Run.model_validate(self._with_pipeline(dict(row)))

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
            clauses.append("(runs.scenario_name = ? OR runs.scenario_id = ?)")
            parameters.extend((scenario, scenario))
        where = f"WHERE {' AND '.join(clauses)}" if clauses else ""
        parameters.append(limit)
        with self.database.connect() as connection:
            rows = connection.execute(
                f"""SELECT runs.id, runs.status, runs.repository_id, runs.branch,
                           repositories.name AS repository_name, runs.build_id,
                           builds.branch AS build_branch, runs.commit_sha,
                           runs.scenario_name, runs.scenario_type, runs.scenario_id, runs.scenario_source,
                           runs.scenario_sha256, runs.autopilot,
                           runs.execution_variant, runs.execution_mode, runs.variants,
                           runs.comparison_id, runs.created_at,
                           runs.wall_time_sec, runs.current_stage
                    FROM runs
                    LEFT JOIN repositories ON repositories.id = runs.repository_id
                    LEFT JOIN builds ON builds.id = runs.build_id
                    {where} ORDER BY runs.id DESC LIMIT ?""",
                parameters,
            ).fetchall()
        return [RunSummary.model_validate(self._with_json_fields(dict(row))) for row in rows]

    def set_variant_metadata(
        self,
        run_id: int,
        *,
        execution_mode: str,
        variants: list[str],
        results: dict[str, dict[str, object]],
        parameters: dict[str, dict[str, float]] | None = None,
    ) -> None:
        with self.database.connect() as connection:
            connection.execute(
                """UPDATE runs
                   SET execution_mode = ?, variants = ?, variant_results = ?,
                       variant_parameters = ?
                   WHERE id = ?""",
                (
                    execution_mode,
                    json.dumps(variants, separators=(",", ":")),
                    json.dumps(results, separators=(",", ":")),
                    json.dumps(parameters or {}, separators=(",", ":")),
                    run_id,
                ),
            )

    def set_pipeline(
        self,
        run_id: int,
        *,
        current_stage: str | None,
        stages: Iterable[PipelineStage],
    ) -> None:
        payload = json.dumps(
            [stage.model_dump(mode="json") for stage in stages],
            separators=(",", ":"),
        )
        with self.database.connect() as connection:
            connection.execute(
                "UPDATE runs SET current_stage = ?, pipeline_stages = ? WHERE id = ?",
                (current_stage, payload, run_id),
            )

    def set_output_directory(self, run_id: int, relative_path: str) -> None:
        with self.database.connect() as connection:
            connection.execute(
                "UPDATE runs SET output_directory = ? WHERE id = ?",
                (relative_path, run_id),
            )

    def set_scenario_path(self, run_id: int, scenario_path: str) -> None:
        with self.database.connect() as connection:
            connection.execute(
                "UPDATE runs SET scenario_path = ? WHERE id = ?",
                (scenario_path, run_id),
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
                f"UPDATE runs SET {assignments} WHERE id = ? AND status IN ({placeholders})",
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

    def delete_terminal(self, run_id: int) -> bool:
        """Delete a completed/failed Run and its cascading persistence children."""
        with self.database.connect() as connection:
            cursor = connection.execute(
                "DELETE FROM runs WHERE id = ? AND status IN ('completed', 'failed')",
                (run_id,),
            )
            return cursor.rowcount == 1

    @staticmethod
    def _with_pipeline(values: dict[str, Any]) -> dict[str, Any]:
        raw = values.pop("pipeline_stages", "[]") or "[]"
        try:
            values["stages"] = json.loads(raw)
        except (TypeError, json.JSONDecodeError):
            values["stages"] = []
        return RunRepository._with_json_fields(values)

    @staticmethod
    def _with_json_fields(values: dict[str, Any]) -> dict[str, Any]:
        for field, fallback in (
            ("controller_parameters", {}),
            ("controller_parameter_overrides", {}),
            ("variants", []),
            ("variant_results", {}),
            ("variant_parameters", {}),
        ):
            raw_parameters = values.get(field) or json.dumps(fallback)
            try:
                values[field] = json.loads(raw_parameters)
            except (TypeError, json.JSONDecodeError):
                values[field] = fallback.copy()
        if not values.get("variants") and values.get("execution_variant"):
            values["variants"] = [values["execution_variant"]]
        return values
