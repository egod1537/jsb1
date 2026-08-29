from __future__ import annotations

from typing import Any, Iterable

from app.domain.build import Build, BuildStatus
from app.repositories.database import Database
from app.repositories.runs import InvalidStatusTransition, utc_now


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
        return Build.model_validate(dict(row))

    def list(self, *, repository_id: int | None = None, limit: int = 100) -> list[Build]:
        where = "WHERE b.repository_id = ?" if repository_id is not None else ""
        parameters: list[Any] = [repository_id] if repository_id is not None else []
        parameters.append(limit)
        with self.database.connect() as connection:
            rows = connection.execute(
                f"""SELECT b.*, r.name AS repository_name
                    FROM builds b JOIN repositories r ON r.id = b.repository_id
                    {where} ORDER BY b.id DESC LIMIT ?""",  # noqa: S608
                parameters,
            ).fetchall()
        return [Build.model_validate(dict(row)) for row in rows]

    def find_completed(self, repository_id: int, commit_sha: str) -> Build | None:
        with self.database.connect() as connection:
            row = connection.execute(
                """SELECT b.*, r.name AS repository_name
                   FROM builds b JOIN repositories r ON r.id = b.repository_id
                   WHERE b.repository_id = ? AND b.commit_sha = ? AND b.status = 'completed'
                   ORDER BY b.id DESC LIMIT 1""",
                (repository_id, commit_sha),
            ).fetchone()
        return Build.model_validate(dict(row)) if row is not None else None

    def transition(
        self,
        build_id: int,
        *,
        expected: Iterable[BuildStatus],
        status: BuildStatus,
        **fields: Any,
    ) -> Build:
        values = {"status": status.value, **fields}
        assignments = ", ".join(f"{name} = ?" for name in values)
        expected_values = [item.value for item in expected]
        placeholders = ", ".join("?" for _ in expected_values)
        with self.database.connect() as connection:
            cursor = connection.execute(
                f"UPDATE builds SET {assignments} WHERE id = ? AND status IN ({placeholders})",  # noqa: S608
                [*values.values(), build_id, *expected_values],
            )
            if cursor.rowcount != 1:
                raise InvalidStatusTransition(
                    f"invalid transition for build {build_id} to {status}"
                )
        return self.get(build_id)

    def fail_incomplete_from_previous_process(self) -> int:
        with self.database.connect() as connection:
            cursor = connection.execute(
                """UPDATE builds SET status = 'failed', completed_at = ?,
                          error_message = 'backend restarted before build completed'
                   WHERE status IN ('queued', 'running')""",
                (utc_now(),),
            )
            return cursor.rowcount
