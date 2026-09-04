from __future__ import annotations

import json
from collections.abc import Iterable
from dataclasses import dataclass
from typing import Any

from app.domain.artifacts import StoredArtifact
from app.domain.clock import utc_now
from app.domain.errors import InvalidStatusTransition
from app.domain.execution import FrozenRunPreparation
from app.domain.lifecycle import ensure_transition
from app.domain.models import (
    RUN_STATUS_TRANSITIONS,
    Metric,
    Run,
    RunStatus,
    RunSummary,
)
from app.domain.pipeline import PipelineStage
from app.repositories.database import Database


@dataclass(frozen=True)
class RunDeletionResult:
    deleted: bool
    orphaned_comparison_id: int | None = None


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
        contract_version: str | None = None,
    ) -> Run:
        with self.database.connect() as connection:
            cursor = connection.execute(
                """INSERT INTO runs
                   (status, repository_id, branch, build_id, commit_sha, scenario_name,
                    scenario_type, scenario_path, autopilot, execution_variant,
                    comparison_id, scenario_id, scenario_source, scenario_sha256,
                    controller_parameters, controller_parameter_overrides,
                    execution_mode, variants, variant_parameters, contract_version,
                    created_at)
                   VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
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
                    json.dumps(
                        controller_parameter_overrides or {}, separators=(",", ":")
                    ),
                    execution_mode,
                    json.dumps(
                        variants or [execution_variant or autopilot],
                        separators=(",", ":"),
                    ),
                    json.dumps(variant_parameters or {}, separators=(",", ":")),
                    contract_version,
                    utc_now(),
                ),
            )
            run_id = int(cursor.lastrowid)
        return self.get(run_id)

    def ids_with_statuses(self, statuses: Iterable[RunStatus]) -> list[int]:
        values = [status.value for status in statuses]
        if not values:
            return []
        placeholders = ", ".join("?" for _ in values)
        with self.database.connect() as connection:
            rows = connection.execute(
                f"SELECT id FROM runs WHERE status IN ({placeholders}) ORDER BY id",
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
                     AND runs.output_directory IS NOT NULL
                   ORDER BY runs.id
                   LIMIT ?""",
                (limit,),
            ).fetchall()
        return [(int(row["id"]), row["build_status"]) for row in rows]

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
        return [
            RunSummary.model_validate(self._with_json_fields(dict(row))) for row in rows
        ]

    def record_runtime_ingestion(
        self,
        run_id: int,
        *,
        results: dict[str, dict[str, object]],
        artifacts: Iterable[StoredArtifact],
    ) -> None:
        with self.database.connect() as connection:
            connection.execute(
                "UPDATE runs SET variant_results = ? WHERE id = ?",
                (
                    json.dumps(results, separators=(",", ":")),
                    run_id,
                ),
            )
            connection.executemany(
                """INSERT INTO artifacts(run_id, kind, path, sha256, size_bytes)
                   VALUES (?, ?, ?, ?, ?)
                   ON CONFLICT(run_id, kind) DO UPDATE SET
                     path = excluded.path,
                     sha256 = excluded.sha256,
                     size_bytes = excluded.size_bytes""",
                [
                    (
                        run_id,
                        artifact.kind,
                        artifact.relative_path,
                        artifact.sha256,
                        artifact.size_bytes,
                    )
                    for artifact in artifacts
                ],
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

    def finalize_preparation(
        self,
        run_id: int,
        *,
        scenario_path: str,
        scenario_sha256: str,
        output_directory: str,
        parameter_snapshot_path: str | None,
        parameter_snapshot_sha256: str | None,
        artifacts: Iterable[StoredArtifact],
    ) -> Run:
        """Atomically expose all frozen inputs as ready for worker claim."""
        preparation = FrozenRunPreparation(
            run_id=run_id,
            scenario_path=scenario_path,
            scenario_sha256=scenario_sha256,
            output_directory=output_directory,
            parameter_snapshot_path=parameter_snapshot_path,
            parameter_snapshot_sha256=parameter_snapshot_sha256,
            artifacts=tuple(artifacts),
        )
        with self.database.connect() as connection:
            cursor = connection.execute(
                """UPDATE runs
                   SET scenario_path = ?, scenario_sha256 = ?,
                       parameter_snapshot_path = ?, parameter_snapshot_sha256 = ?,
                       output_directory = ?
                   WHERE id = ? AND status = ? AND output_directory IS NULL""",
                (
                    scenario_path,
                    scenario_sha256,
                    parameter_snapshot_path,
                    parameter_snapshot_sha256,
                    output_directory,
                    run_id,
                    RunStatus.QUEUED.value,
                ),
            )
            if cursor.rowcount != 1:
                raise InvalidStatusTransition(
                    f"run {run_id} cannot finalize execution preparation"
                )
            connection.executemany(
                """INSERT INTO artifacts(run_id, kind, path, sha256, size_bytes)
                   VALUES (?, ?, ?, ?, ?)
                   ON CONFLICT(run_id, kind) DO UPDATE SET
                     path = excluded.path,
                     sha256 = excluded.sha256,
                     size_bytes = excluded.size_bytes""",
                [
                    (
                        run_id,
                        artifact.kind,
                        artifact.relative_path,
                        artifact.sha256,
                        artifact.size_bytes,
                    )
                    for artifact in preparation.artifacts
                ],
            )
        return self.get(run_id)

    def transition(
        self,
        run_id: int,
        *,
        expected: Iterable[RunStatus],
        status: RunStatus,
        **fields: Any,
    ) -> Run:
        expected_statuses = tuple(expected)
        if not expected_statuses:
            raise InvalidStatusTransition("expected run statuses must not be empty")
        for current in expected_statuses:
            ensure_transition(
                current,
                status,
                RUN_STATUS_TRANSITIONS,
                entity="run",
            )
        permitted_fields = {
            RunStatus.RUNNING: {"started_at"},
            RunStatus.COMPLETED: {
                "finished_at",
                "exit_code",
                "wall_time_sec",
                "simulation_time_sec",
                "error_message",
            },
            RunStatus.FAILED: {
                "finished_at",
                "exit_code",
                "wall_time_sec",
                "error_message",
            },
        }[status]
        unexpected_fields = set(fields) - permitted_fields
        if unexpected_fields:
            raise ValueError(
                "invalid lifecycle fields: " + ", ".join(sorted(unexpected_fields))
            )
        values = {"status": status.value, **fields}
        assignments = ", ".join(f"{name} = ?" for name in values)
        expected_values = [item.value for item in expected_statuses]
        placeholders = ", ".join("?" for _ in expected_values)
        with self.database.connect() as connection:
            cursor = connection.execute(
                f"UPDATE runs SET {assignments} WHERE id = ? AND status IN ({placeholders})",
                [*values.values(), run_id, *expected_values],
            )
            if cursor.rowcount != 1:
                raise InvalidStatusTransition(
                    f"invalid transition for run {run_id} to {status}"
                )
        return self.get(run_id)

    def mark_running(self, run_id: int, *, started_at: str) -> Run:
        return self.transition(
            run_id,
            expected=[RunStatus.QUEUED],
            status=RunStatus.RUNNING,
            started_at=started_at,
        )

    def claim_for_execution(self, run_id: int, *, started_at: str) -> Run | None:
        """Atomically claim one queued Run; duplicate workers receive ``None``."""
        with self.database.connect() as connection:
            cursor = connection.execute(
                """UPDATE runs SET status = ?, started_at = ?
                   WHERE id = ? AND status = ? AND output_directory IS NOT NULL""",
                (
                    RunStatus.RUNNING.value,
                    started_at,
                    run_id,
                    RunStatus.QUEUED.value,
                ),
            )
            if cursor.rowcount != 1:
                return None
        return self.get(run_id)

    def complete_run(
        self,
        run_id: int,
        *,
        finished_at: str,
        exit_code: int,
        wall_time_sec: float,
        simulation_time_sec: float | None,
        error_message: str | None = None,
    ) -> Run:
        return self.transition(
            run_id,
            expected=[RunStatus.RUNNING],
            status=RunStatus.COMPLETED,
            finished_at=finished_at,
            exit_code=exit_code,
            wall_time_sec=wall_time_sec,
            simulation_time_sec=simulation_time_sec,
            error_message=error_message,
        )

    def fail_run(
        self,
        run_id: int,
        *,
        finished_at: str,
        error_message: str,
        exit_code: int | None = None,
        wall_time_sec: float | None = None,
    ) -> Run:
        return self.transition(
            run_id,
            expected=[RunStatus.QUEUED, RunStatus.RUNNING],
            status=RunStatus.FAILED,
            finished_at=finished_at,
            exit_code=exit_code,
            wall_time_sec=wall_time_sec,
            error_message=error_message,
        )

    def replace_metrics(self, run_id: int, metrics: Iterable[Metric]) -> None:
        with self.database.connect() as connection:
            connection.execute("DELETE FROM metrics WHERE run_id = ?", (run_id,))
            connection.executemany(
                "INSERT INTO metrics(run_id, name, value, unit) VALUES (?, ?, ?, ?)",
                [
                    (run_id, metric.name, metric.value, metric.unit)
                    for metric in metrics
                ],
            )

    def get_metrics(self, run_id: int) -> list[Metric]:
        with self.database.connect() as connection:
            rows = connection.execute(
                "SELECT name, value, unit FROM metrics WHERE run_id = ? ORDER BY id",
                (run_id,),
            ).fetchall()
        return [Metric.model_validate(dict(row)) for row in rows]

    def record_artifact(self, run_id: int, artifact: StoredArtifact) -> None:
        self.record_artifacts(run_id, (artifact,))

    def record_artifacts(
        self, run_id: int, artifacts: Iterable[StoredArtifact]
    ) -> None:
        with self.database.connect() as connection:
            connection.executemany(
                """INSERT INTO artifacts(run_id, kind, path, sha256, size_bytes)
                   VALUES (?, ?, ?, ?, ?)
                   ON CONFLICT(run_id, kind) DO UPDATE SET
                     path = excluded.path,
                     sha256 = excluded.sha256,
                     size_bytes = excluded.size_bytes""",
                [
                    (
                        run_id,
                        artifact.kind,
                        artifact.relative_path,
                        artifact.sha256,
                        artifact.size_bytes,
                    )
                    for artifact in artifacts
                ],
            )

    def get_artifact_rows(self, run_id: int) -> list[dict[str, Any]]:
        with self.database.connect() as connection:
            rows = connection.execute(
                """SELECT id, run_id, kind, path, sha256, size_bytes
                   FROM artifacts WHERE run_id = ? ORDER BY id""",
                (run_id,),
            ).fetchall()
        return [dict(row) for row in rows]

    def get_artifact_row(self, run_id: int, kind: str) -> dict[str, Any]:
        with self.database.connect() as connection:
            row = connection.execute(
                """SELECT id, run_id, kind, path, sha256, size_bytes
                   FROM artifacts WHERE run_id = ? AND kind = ?""",
                (run_id, kind),
            ).fetchone()
        if row is None:
            raise KeyError((run_id, kind))
        return dict(row)

    def delete_terminal(self, run_id: int) -> RunDeletionResult:
        """Atomically delete a terminal Run and any now-empty Comparison."""
        with self.database.connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            row = connection.execute(
                "SELECT comparison_id FROM runs WHERE id = ?", (run_id,)
            ).fetchone()
            comparison_id = (
                int(row["comparison_id"])
                if row is not None and row["comparison_id"] is not None
                else None
            )
            cursor = connection.execute(
                "DELETE FROM runs WHERE id = ? AND status IN ('completed', 'failed')",
                (run_id,),
            )
            if cursor.rowcount != 1:
                return RunDeletionResult(False)
            orphaned_comparison_id = None
            if comparison_id is not None:
                deleted = connection.execute(
                    """DELETE FROM comparisons
                       WHERE id = ?
                         AND NOT EXISTS (
                           SELECT 1 FROM runs WHERE comparison_id = ?
                         )""",
                    (comparison_id, comparison_id),
                )
                if deleted.rowcount == 1:
                    orphaned_comparison_id = comparison_id
            return RunDeletionResult(True, orphaned_comparison_id)

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
