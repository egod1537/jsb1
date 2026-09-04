from __future__ import annotations

from app.domain.build import INSTANCE_STATUS_TRANSITIONS, Instance, InstanceStatus
from app.domain.clock import utc_now
from app.domain.errors import InvalidStatusTransition
from app.domain.lifecycle import ensure_transition
from app.repositories.database import Database


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
        ensure_transition(
            InstanceStatus.QUEUED,
            InstanceStatus.RUNNING,
            INSTANCE_STATUS_TRANSITIONS,
            entity="instance",
        )
        with self.database.connect() as connection:
            cursor = connection.execute(
                """UPDATE instances SET status = ?, pid = ?, started_at = ?
                   WHERE id = ? AND status = ?""",
                (
                    InstanceStatus.RUNNING.value,
                    pid,
                    utc_now(),
                    instance_id,
                    InstanceStatus.QUEUED.value,
                ),
            )
            if cursor.rowcount != 1:
                raise InvalidStatusTransition(
                    f"instance {instance_id} cannot transition to running"
                )

    def finish(self, instance_id: int, *, failed: bool) -> None:
        status = InstanceStatus.FAILED if failed else InstanceStatus.STOPPED
        expected = (
            (InstanceStatus.QUEUED, InstanceStatus.RUNNING)
            if failed
            else (InstanceStatus.RUNNING,)
        )
        for current in expected:
            ensure_transition(
                current,
                status,
                INSTANCE_STATUS_TRANSITIONS,
                entity="instance",
            )
        placeholders = ", ".join("?" for _ in expected)
        with self.database.connect() as connection:
            cursor = connection.execute(
                f"""UPDATE instances SET status = ?, stopped_at = ?
                    WHERE id = ? AND status IN ({placeholders})""",
                (
                    status.value,
                    utc_now(),
                    instance_id,
                    *(item.value for item in expected),
                ),
            )
            if cursor.rowcount != 1:
                raise InvalidStatusTransition(
                    f"instance {instance_id} cannot transition to {status.value}"
                )

    def fail_running_from_previous_worker(self) -> int:
        with self.database.connect() as connection:
            cursor = connection.execute(
                """UPDATE instances SET status = 'failed', stopped_at = ?
                   WHERE status = 'running'""",
                (utc_now(),),
            )
            return cursor.rowcount
