from __future__ import annotations

import sqlite3

from app.domain.clock import utc_now
from app.domain.errors import RepositoryConflict
from app.domain.repository import Repository
from app.repositories.database import Database


class JsbRepositoryRepository:
    def __init__(self, database: Database) -> None:
        self.database = database

    def create(
        self, *, name: str, remote_url: str, local_path: str, default_branch: str
    ) -> Repository:
        timestamp = utc_now()
        try:
            with self.database.connect() as connection:
                cursor = connection.execute(
                    """INSERT INTO repositories
                       (name, remote_url, local_path, default_branch, created_at, updated_at)
                       VALUES (?, ?, ?, ?, ?, ?)""",
                    (name, remote_url, local_path, default_branch, timestamp, timestamp),
                )
                repository_id = int(cursor.lastrowid)
        except sqlite3.IntegrityError as exc:
            raise RepositoryConflict("repository name or local path is already registered") from exc
        return self.get(repository_id)

    def get(self, repository_id: int) -> Repository:
        with self.database.connect() as connection:
            row = connection.execute(
                "SELECT * FROM repositories WHERE id = ?", (repository_id,)
            ).fetchone()
        if row is None:
            raise KeyError(repository_id)
        return Repository.model_validate(dict(row))

    def get_by_name(self, name: str) -> Repository:
        with self.database.connect() as connection:
            row = connection.execute(
                "SELECT * FROM repositories WHERE name = ? COLLATE NOCASE", (name,)
            ).fetchone()
        if row is None:
            raise KeyError(name)
        return Repository.model_validate(dict(row))

    def list(self) -> list[Repository]:
        with self.database.connect() as connection:
            rows = connection.execute(
                "SELECT * FROM repositories ORDER BY name COLLATE NOCASE"
            ).fetchall()
        return [Repository.model_validate(dict(row)) for row in rows]

    def mark_fetched(self, repository_id: int) -> Repository:
        timestamp = utc_now()
        with self.database.connect() as connection:
            connection.execute(
                "UPDATE repositories SET last_fetched_at = ?, updated_at = ? WHERE id = ?",
                (timestamp, timestamp, repository_id),
            )
        return self.get(repository_id)

    def update_configuration(
        self,
        repository_id: int,
        *,
        remote_url: str,
        local_path: str,
        default_branch: str,
    ) -> Repository:
        timestamp = utc_now()
        try:
            with self.database.connect() as connection:
                cursor = connection.execute(
                    """UPDATE repositories
                       SET remote_url = ?, local_path = ?, default_branch = ?, updated_at = ?
                       WHERE id = ?""",
                    (
                        remote_url,
                        local_path,
                        default_branch,
                        timestamp,
                        repository_id,
                    ),
                )
                if cursor.rowcount != 1:
                    raise KeyError(repository_id)
        except sqlite3.IntegrityError as exc:
            raise RepositoryConflict(
                "configured JSB0 repository path conflicts with an existing repository"
            ) from exc
        return self.get(repository_id)

    def delete(self, repository_id: int) -> None:
        try:
            with self.database.connect() as connection:
                cursor = connection.execute(
                    "DELETE FROM repositories WHERE id = ?", (repository_id,)
                )
                if cursor.rowcount != 1:
                    raise KeyError(repository_id)
        except sqlite3.IntegrityError as exc:
            raise RepositoryConflict(
                "repository has builds or runs and cannot be deleted"
            ) from exc
