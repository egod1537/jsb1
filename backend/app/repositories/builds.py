from __future__ import annotations

import json
from collections.abc import Callable, Iterable
from typing import Any

from app.domain.build import BUILD_STATUS_TRANSITIONS, Build, BuildStatus
from app.domain.clock import utc_now
from app.domain.errors import InvalidStatusTransition
from app.domain.lifecycle import ensure_transition
from app.domain.pipeline import PipelineStage
from app.repositories.database import Database


class BuildRepository:
    def __init__(self, database: Database) -> None:
        self.database = database

    def create(
        self,
        *,
        repository_id: int,
        commit_sha: str,
        branch: str | None,
        build_dir: str,
        stdout_path: str,
        stderr_path: str,
    ) -> Build:
        with self.database.connect() as connection:
            cursor = connection.execute(
                """INSERT INTO builds
                   (repository_id, commit_sha, branch, status, build_dir,
                    stdout_path, stderr_path, created_at)
                   VALUES (?, ?, ?, ?, ?, ?, ?, ?)""",
                (
                    repository_id,
                    commit_sha,
                    branch,
                    BuildStatus.QUEUED.value,
                    build_dir,
                    stdout_path,
                    stderr_path,
                    utc_now(),
                ),
            )
            build_id = int(cursor.lastrowid)
            connection.execute(
                """INSERT INTO build_keys(repository_id, commit_sha, build_id)
                   VALUES (?, ?, ?)
                   ON CONFLICT(repository_id, commit_sha) DO UPDATE SET
                     build_id = excluded.build_id""",
                (repository_id, commit_sha, build_id),
            )
        return self.get(build_id)

    def set_paths(
        self,
        build_id: int,
        *,
        build_dir: str,
        stdout_path: str,
        stderr_path: str,
    ) -> Build:
        with self.database.connect() as connection:
            connection.execute(
                """UPDATE builds SET build_dir = ?, stdout_path = ?, stderr_path = ?
                   WHERE id = ?""",
                (build_dir, stdout_path, stderr_path, build_id),
            )
        return self.get(build_id)

    def get(self, build_id: int) -> Build:
        with self.database.connect() as connection:
            row = connection.execute(
                """SELECT b.*, r.name AS repository_name
                   FROM builds b JOIN repositories r ON r.id = b.repository_id
                   WHERE b.id = ?""",
                (build_id,),
            ).fetchone()
        if row is None:
            raise KeyError(build_id)
        return Build.model_validate(self._with_pipeline(dict(row)))

    def list(
        self, *, repository_id: int | None = None, limit: int = 100
    ) -> list[Build]:
        where = "WHERE b.repository_id = ?" if repository_id is not None else ""
        parameters: list[Any] = [repository_id] if repository_id is not None else []
        parameters.append(limit)
        with self.database.connect() as connection:
            rows = connection.execute(
                f"""SELECT b.*, r.name AS repository_name
                    FROM builds b JOIN repositories r ON r.id = b.repository_id
                    {where} ORDER BY b.id DESC LIMIT ?""",
                parameters,
            ).fetchall()
        return [Build.model_validate(self._with_pipeline(dict(row))) for row in rows]

    def set_pipeline(
        self,
        build_id: int,
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
                "UPDATE builds SET current_stage = ?, pipeline_stages = ? WHERE id = ?",
                (current_stage, payload, build_id),
            )

    def reserve(
        self,
        *,
        repository_id: int,
        commit_sha: str,
        branch: str | None,
        rebuild: bool,
        paths_for_id: Callable[[int], tuple[str, str, str]],
    ) -> tuple[Build, bool]:
        """Atomically share an active/completed build or reserve a new one.

        ``BEGIN IMMEDIATE`` serializes the lookup+insert boundary across API
        processes. Paths are derived from the allocated id before the row is
        committed, so an external worker can never observe a half-initialized
        queued build.
        """
        selected_id: int
        reused = False
        with self.database.connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            row = connection.execute(
                """SELECT builds.id, builds.status
                   FROM build_keys
                   JOIN builds ON builds.id = build_keys.build_id
                   WHERE build_keys.repository_id = ?
                     AND build_keys.commit_sha = ?""",
                (repository_id, commit_sha),
            ).fetchone()
            reusable = row is not None and (
                row["status"]
                in {
                    BuildStatus.QUEUED.value,
                    BuildStatus.RUNNING.value,
                }
                or (row["status"] == BuildStatus.COMPLETED.value and not rebuild)
            )
            if reusable:
                selected_id = int(row["id"])
                reused = True
            else:
                cursor = connection.execute(
                    """INSERT INTO builds
                       (repository_id, commit_sha, branch, status, build_dir,
                        stdout_path, stderr_path, created_at)
                       VALUES (?, ?, ?, ?, '', '', '', ?)""",
                    (
                        repository_id,
                        commit_sha,
                        branch,
                        BuildStatus.QUEUED.value,
                        utc_now(),
                    ),
                )
                selected_id = int(cursor.lastrowid)
                build_dir, stdout_path, stderr_path = paths_for_id(selected_id)
                connection.execute(
                    """UPDATE builds
                       SET build_dir = ?, stdout_path = ?, stderr_path = ?
                       WHERE id = ?""",
                    (build_dir, stdout_path, stderr_path, selected_id),
                )
                connection.execute(
                    """INSERT INTO build_keys(repository_id, commit_sha, build_id)
                       VALUES (?, ?, ?)
                       ON CONFLICT(repository_id, commit_sha) DO UPDATE SET
                         build_id = excluded.build_id""",
                    (repository_id, commit_sha, selected_id),
                )
        return self.get(selected_id), reused

    def list_ids(self, status: BuildStatus, *, limit: int = 100) -> list[int]:
        with self.database.connect() as connection:
            rows = connection.execute(
                "SELECT id FROM builds WHERE status = ? ORDER BY id LIMIT ?",
                (status.value, limit),
            ).fetchall()
        return [int(row["id"]) for row in rows]

    def ids_with_statuses(self, statuses: Iterable[BuildStatus]) -> list[int]:
        values = [status.value for status in statuses]
        if not values:
            return []
        placeholders = ", ".join("?" for _ in values)
        with self.database.connect() as connection:
            rows = connection.execute(
                f"SELECT id FROM builds WHERE status IN ({placeholders}) ORDER BY id",
                values,
            ).fetchall()
        return [int(row["id"]) for row in rows]

    def transition(
        self,
        build_id: int,
        *,
        expected: Iterable[BuildStatus],
        status: BuildStatus,
        **fields: Any,
    ) -> Build:
        expected_statuses = tuple(expected)
        if not expected_statuses:
            raise InvalidStatusTransition("expected build statuses must not be empty")
        for current in expected_statuses:
            ensure_transition(
                current,
                status,
                BUILD_STATUS_TRANSITIONS,
                entity="build",
            )
        permitted_fields = {
            BuildStatus.RUNNING: {"started_at"},
            BuildStatus.COMPLETED: {
                "executable_path",
                "completed_at",
                "error_message",
            },
            BuildStatus.FAILED: {"completed_at", "error_message"},
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
                f"UPDATE builds SET {assignments} WHERE id = ? AND status IN ({placeholders})",
                [*values.values(), build_id, *expected_values],
            )
            if cursor.rowcount != 1:
                raise InvalidStatusTransition(
                    f"invalid transition for build {build_id} to {status}"
                )
        return self.get(build_id)

    def mark_running(self, build_id: int, *, started_at: str) -> Build:
        return self.transition(
            build_id,
            expected=[BuildStatus.QUEUED],
            status=BuildStatus.RUNNING,
            started_at=started_at,
        )

    def claim_for_build(self, build_id: int, *, started_at: str) -> Build | None:
        """Atomically claim one queued Build; duplicate workers receive ``None``."""
        with self.database.connect() as connection:
            cursor = connection.execute(
                """UPDATE builds SET status = ?, started_at = ?
                   WHERE id = ? AND status = ?""",
                (
                    BuildStatus.RUNNING.value,
                    started_at,
                    build_id,
                    BuildStatus.QUEUED.value,
                ),
            )
            if cursor.rowcount != 1:
                return None
        return self.get(build_id)

    def complete_build(
        self,
        build_id: int,
        *,
        executable_path: str,
        completed_at: str,
    ) -> Build:
        return self.transition(
            build_id,
            expected=[BuildStatus.RUNNING],
            status=BuildStatus.COMPLETED,
            executable_path=executable_path,
            completed_at=completed_at,
            error_message=None,
        )

    def fail_build(
        self, build_id: int, *, completed_at: str, error_message: str
    ) -> Build:
        return self.transition(
            build_id,
            expected=[BuildStatus.QUEUED, BuildStatus.RUNNING],
            status=BuildStatus.FAILED,
            completed_at=completed_at,
            error_message=error_message,
        )

    def fail_running_from_previous_worker(self) -> int:
        with self.database.connect() as connection:
            cursor = connection.execute(
                """UPDATE builds SET status = 'failed', completed_at = ?,
                          error_message = 'execution worker restarted during build'
                   WHERE status = 'running'""",
                (utc_now(),),
            )
            return cursor.rowcount

    @staticmethod
    def _with_pipeline(values: dict[str, Any]) -> dict[str, Any]:
        raw = values.pop("pipeline_stages", "[]") or "[]"
        try:
            values["stages"] = json.loads(raw)
        except (TypeError, json.JSONDecodeError):
            values["stages"] = []
        return values
