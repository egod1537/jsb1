from __future__ import annotations

import sqlite3
from collections.abc import Iterable

from app.domain.clock import utc_now
from app.domain.deployment import (
    DEPLOYMENT_STATUS_TRANSITIONS,
    BranchDeployment,
    DeploymentStatus,
)
from app.domain.errors import (
    DeploymentConflict,
    InvalidDeploymentTransition,
    NoDeploymentPortAvailable,
)
from app.domain.lifecycle import ensure_transition
from app.repositories.database import Database


class DeploymentRepository:
    ACTIVE = (
        DeploymentStatus.QUEUED,
        DeploymentStatus.STARTING,
        DeploymentStatus.RUNNING,
    )

    def __init__(self, database: Database) -> None:
        self.database = database

    def create(
        self,
        *,
        repository_id: int,
        branch: str,
        commit_sha: str,
        slug: str,
        hostname: str,
        worktree_path: str,
    ) -> BranchDeployment:
        timestamp = utc_now()
        try:
            with self.database.connect() as connection:
                cursor = connection.execute(
                    """INSERT INTO deployments
                       (repository_id, branch, commit_sha, slug, hostname, status,
                        compose_project, worktree_path, created_at, updated_at)
                       VALUES (?, ?, ?, ?, ?, ?, 'pending', ?, ?, ?)""",
                    (
                        repository_id,
                        branch,
                        commit_sha,
                        slug,
                        hostname,
                        DeploymentStatus.QUEUED.value,
                        worktree_path,
                        timestamp,
                        timestamp,
                    ),
                )
                deployment_id = int(cursor.lastrowid)
                compose_project = f"jsb1-{slug}-{deployment_id}"
                connection.execute(
                    "UPDATE deployments SET compose_project = ? WHERE id = ?",
                    (compose_project, deployment_id),
                )
        except sqlite3.IntegrityError as exc:
            raise DeploymentConflict("deployment could not be created") from exc
        return self.get(deployment_id)

    def get(self, deployment_id: int) -> BranchDeployment:
        with self.database.connect() as connection:
            row = connection.execute(
                "SELECT * FROM deployments WHERE id = ?", (deployment_id,)
            ).fetchone()
        if row is None:
            raise KeyError(deployment_id)
        return BranchDeployment.model_validate(dict(row))

    def list(
        self,
        *,
        repository_id: int | None = None,
        limit: int = 200,
    ) -> list[BranchDeployment]:
        where = "WHERE repository_id = ?" if repository_id is not None else ""
        parameters: list[int] = [] if repository_id is None else [repository_id]
        parameters.append(limit)
        with self.database.connect() as connection:
            rows = connection.execute(
                f"SELECT * FROM deployments {where} ORDER BY id DESC LIMIT ?",
                parameters,
            ).fetchall()
        return [BranchDeployment.model_validate(dict(row)) for row in rows]

    def active_for_branch(
        self, repository_id: int, branch: str
    ) -> BranchDeployment | None:
        with self.database.connect() as connection:
            row = connection.execute(
                """SELECT * FROM deployments
                   WHERE repository_id = ? AND branch = ?
                     AND status IN ('queued', 'starting', 'running')
                   ORDER BY id DESC LIMIT 1""",
                (repository_id, branch),
            ).fetchone()
        return BranchDeployment.model_validate(dict(row)) if row else None

    def latest_for_branch(
        self, repository_id: int, branch: str
    ) -> BranchDeployment | None:
        with self.database.connect() as connection:
            row = connection.execute(
                """SELECT * FROM deployments
                   WHERE repository_id = ? AND branch = ?
                   ORDER BY id DESC LIMIT 1""",
                (repository_id, branch),
            ).fetchone()
        return BranchDeployment.model_validate(dict(row)) if row else None

    def slug_owner(
        self, slug: str, *, repository_id: int, branch: str
    ) -> BranchDeployment | None:
        with self.database.connect() as connection:
            row = connection.execute(
                """SELECT * FROM deployments
                   WHERE slug = ? AND NOT (repository_id = ? AND branch = ?)
                   ORDER BY id DESC LIMIT 1""",
                (slug, repository_id, branch),
            ).fetchone()
        return BranchDeployment.model_validate(dict(row)) if row else None

    def active_for_hostname(self, hostname: str) -> BranchDeployment | None:
        with self.database.connect() as connection:
            row = connection.execute(
                """SELECT * FROM deployments
                   WHERE hostname = ? AND status IN ('queued', 'starting', 'running')
                   ORDER BY id DESC LIMIT 1""",
                (hostname,),
            ).fetchone()
        return BranchDeployment.model_validate(dict(row)) if row else None

    def current_for_hostname(self, hostname: str) -> BranchDeployment | None:
        with self.database.connect() as connection:
            row = connection.execute(
                """SELECT * FROM deployments
                   WHERE hostname = ? AND status = 'running'
                   ORDER BY id DESC LIMIT 1""",
                (hostname,),
            ).fetchone()
        return BranchDeployment.model_validate(dict(row)) if row else None

    def reserve_ports(
        self,
        deployment_id: int,
        *,
        port_start: int,
        port_end: int,
        unavailable_ports: set[int] | None = None,
    ) -> BranchDeployment:
        if port_end - port_start < 1:
            raise NoDeploymentPortAvailable(
                "deployment port range must contain two ports"
            )
        timestamp = utc_now()
        with self.database.connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            row = connection.execute(
                "SELECT status FROM deployments WHERE id = ?", (deployment_id,)
            ).fetchone()
            if row is None:
                raise KeyError(deployment_id)
            if row["status"] != DeploymentStatus.QUEUED.value:
                raise InvalidDeploymentTransition(
                    f"deployment {deployment_id} is not queued"
                )
            used_rows = connection.execute(
                """SELECT frontend_port, backend_port FROM deployments
                   WHERE id != ? AND status IN ('queued', 'starting', 'running')""",
                (deployment_id,),
            ).fetchall()
            used = set(unavailable_ports or ()) | {
                int(port)
                for used_row in used_rows
                for port in (used_row["frontend_port"], used_row["backend_port"])
                if port is not None
            }
            available = [
                port for port in range(port_start, port_end + 1) if port not in used
            ]
            if len(available) < 2:
                raise NoDeploymentPortAvailable("no deployment port pair is available")
            frontend_port, backend_port = available[:2]
            cursor = connection.execute(
                """UPDATE deployments
                   SET status = 'starting', frontend_port = ?, backend_port = ?,
                       updated_at = ?, error_message = NULL
                   WHERE id = ? AND status = 'queued'""",
                (frontend_port, backend_port, timestamp, deployment_id),
            )
            if cursor.rowcount != 1:
                raise InvalidDeploymentTransition(
                    f"deployment {deployment_id} could not reserve ports"
                )
        return self.get(deployment_id)

    def transition(
        self,
        deployment_id: int,
        *,
        expected: Iterable[DeploymentStatus],
        status: DeploymentStatus,
        error_message: str | None = None,
    ) -> BranchDeployment:
        expected_statuses = tuple(expected)
        if not expected_statuses:
            raise ValueError("expected statuses must not be empty")
        for current in expected_statuses:
            ensure_transition(
                current,
                status,
                DEPLOYMENT_STATUS_TRANSITIONS,
                entity="deployment",
                error_type=InvalidDeploymentTransition,
            )
        expected_values = [item.value for item in expected_statuses]
        timestamp = utc_now()
        assignments = ["status = ?", "updated_at = ?", "error_message = ?"]
        values: list[object] = [status.value, timestamp, error_message]
        if status is DeploymentStatus.RUNNING:
            assignments.append("started_at = COALESCE(started_at, ?)")
            values.append(timestamp)
        if status is DeploymentStatus.STOPPED:
            assignments.append("stopped_at = ?")
            values.append(timestamp)
        placeholders = ", ".join("?" for _ in expected_values)
        with self.database.connect() as connection:
            cursor = connection.execute(
                f"""UPDATE deployments SET {", ".join(assignments)}
                    WHERE id = ? AND status IN ({placeholders})""",
                [*values, deployment_id, *expected_values],
            )
            if cursor.rowcount != 1:
                raise InvalidDeploymentTransition(
                    f"invalid transition for deployment {deployment_id} to {status.value}"
                )
        return self.get(deployment_id)

    def fail_interrupted(self) -> int:
        timestamp = utc_now()
        with self.database.connect() as connection:
            cursor = connection.execute(
                """UPDATE deployments
                   SET status = 'failed', updated_at = ?,
                       error_message = 'backend restarted while deployment was starting'
                   WHERE status IN ('queued', 'starting')""",
                (timestamp,),
            )
            return cursor.rowcount
