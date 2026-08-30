from __future__ import annotations

from app.domain.build import Instance, InstanceStatus
from app.repositories.database import Database
from app.repositories.runs import utc_now


class InstanceRepository:
    def __init__(self, database: Database) -> None:
        self.database = database

    def create(self, *, build_id: int, run_id: int) -> Instance:
        with self.database.connect() as connection:
            cursor = connection.execute(
                "INSERT INTO instances(build_id, run_id, status) VALUES (?, ?, ?)",
                (build_id, run_id, InstanceStatus.QUEUED.value),
            )
            instance_id = int(cursor.lastrowid)
        return self.get(instance_id)

    def get(self, instance_id: int) -> Instance:
        with self.database.connect() as connection:
            row = connection.execute(
                "SELECT * FROM instances WHERE id = ?", (instance_id,)
            ).fetchone()
        if row is None:
            raise KeyError(instance_id)
        return Instance.model_validate(dict(row))

    def get_for_run(self, run_id: int) -> Instance | None:
        with self.database.connect() as connection:
            row = connection.execute(
                "SELECT * FROM instances WHERE run_id = ?", (run_id,)
            ).fetchone()
        return Instance.model_validate(dict(row)) if row is not None else None

    def mark_running(self, instance_id: int, pid: int) -> None:
        with self.database.connect() as connection:
            connection.execute(
                """UPDATE instances SET status = ?, pid = ?, started_at = ?
                   WHERE id = ?""",
                (InstanceStatus.RUNNING.value, pid, utc_now(), instance_id),
            )

    def finish(self, instance_id: int, *, failed: bool) -> None:
        status = InstanceStatus.FAILED if failed else InstanceStatus.STOPPED
        with self.database.connect() as connection:
            connection.execute(
                "UPDATE instances SET status = ?, stopped_at = ? WHERE id = ?",
                (status.value, utc_now(), instance_id),
            )

    def fail_incomplete_from_previous_process(self) -> int:
        with self.database.connect() as connection:
            cursor = connection.execute(
                """UPDATE instances SET status = 'failed', stopped_at = ?
                   WHERE status IN ('queued', 'running')""",
                (utc_now(),),
            )
            return cursor.rowcount

    def fail_running_from_previous_worker(self) -> int:
        with self.database.connect() as connection:
            cursor = connection.execute(
                """UPDATE instances SET status = 'failed', stopped_at = ?
                   WHERE status = 'running'""",
                (utc_now(),),
            )
            return cursor.rowcount
