from __future__ import annotations

import json
from collections.abc import Iterable
from datetime import datetime, timezone
from typing import Any

from app.repositories.database import Database


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


class ScenarioCatalogRepository:
    def __init__(self, database: Database) -> None:
        self.database = database

    def upsert_valid(
        self,
        *,
        source: str,
        scenario_id: str,
        cache_path: str,
        name: str,
        autopilot: str | None,
        sha256: str,
        validated_commit: str,
        synced_at: str,
    ) -> bool:
        previous = self.get(source, scenario_id)
        changed = previous is None or previous.get("sha256") != sha256 or not previous.get("active")
        with self.database.connect() as connection:
            connection.execute(
                """INSERT INTO scenario_catalog
                   (source, scenario_id, relative_path, cache_path, name, autopilot,
                    sha256, valid, active, validated_commit, last_validated_at,
                    last_sync_at, last_error, last_error_commit, last_error_at)
                   VALUES (?, ?, ?, ?, ?, ?, ?, 1, 1, ?, ?, ?, NULL, NULL, NULL)
                   ON CONFLICT(source, scenario_id) DO UPDATE SET
                     relative_path=excluded.relative_path,
                     cache_path=excluded.cache_path,
                     name=excluded.name,
                     autopilot=excluded.autopilot,
                     sha256=excluded.sha256,
                     valid=1,
                     active=1,
                     validated_commit=excluded.validated_commit,
                     last_validated_at=excluded.last_validated_at,
                     last_sync_at=excluded.last_sync_at,
                     last_error=NULL,
                     last_error_commit=NULL,
                     last_error_at=NULL""",
                (
                    source, scenario_id, scenario_id, cache_path, name, autopilot,
                    sha256, validated_commit, synced_at, synced_at,
                ),
            )
        return changed

    def record_invalid(
        self,
        *,
        source: str,
        scenario_id: str,
        errors: list[dict[str, str]],
        validated_commit: str,
        synced_at: str,
    ) -> None:
        encoded = json.dumps(errors, ensure_ascii=False)
        with self.database.connect() as connection:
            existing = connection.execute(
                "SELECT valid, cache_path FROM scenario_catalog WHERE source=? AND scenario_id=?",
                (source, scenario_id),
            ).fetchone()
            if existing is not None and existing["valid"] and existing["cache_path"]:
                # A rejected update must not replace or hide the last-known-good cache.
                connection.execute(
                    """UPDATE scenario_catalog SET active=1, last_sync_at=?,
                       last_error=?, last_error_commit=?, last_error_at=?
                       WHERE source=? AND scenario_id=?""",
                    (synced_at, encoded, validated_commit, synced_at, source, scenario_id),
                )
            else:
                connection.execute(
                    """INSERT INTO scenario_catalog
                       (source, scenario_id, relative_path, valid, active,
                        last_sync_at, last_error, last_error_commit, last_error_at)
                       VALUES (?, ?, ?, 0, 1, ?, ?, ?, ?)
                       ON CONFLICT(source, scenario_id) DO UPDATE SET
                         valid=0, active=1, last_sync_at=excluded.last_sync_at,
                         last_error=excluded.last_error,
                         last_error_commit=excluded.last_error_commit,
                         last_error_at=excluded.last_error_at""",
                    (source, scenario_id, scenario_id, synced_at, encoded, validated_commit, synced_at),
                )

    def mark_missing(self, source: str, present_ids: Iterable[str], synced_at: str) -> int:
        present = set(present_ids)
        with self.database.connect() as connection:
            rows = connection.execute(
                "SELECT scenario_id FROM scenario_catalog WHERE source=? AND active=1",
                (source,),
            ).fetchall()
            missing = [row["scenario_id"] for row in rows if row["scenario_id"] not in present]
            if missing:
                connection.executemany(
                    "UPDATE scenario_catalog SET active=0, last_sync_at=? WHERE source=? AND scenario_id=?",
                    [(synced_at, source, item) for item in missing],
                )
        return len(missing)

    def list_valid(self, source: str = "sftp") -> list[dict[str, Any]]:
        with self.database.connect() as connection:
            rows = connection.execute(
                """SELECT * FROM scenario_catalog
                   WHERE source=? AND active=1 AND valid=1 AND cache_path IS NOT NULL
                   ORDER BY scenario_id""",
                (source,),
            ).fetchall()
        return [dict(row) for row in rows]

    def list_active(self, source: str = "sftp") -> list[dict[str, Any]]:
        with self.database.connect() as connection:
            rows = connection.execute(
                """SELECT * FROM scenario_catalog
                   WHERE source=? AND active=1 ORDER BY scenario_id""",
                (source,),
            ).fetchall()
        return [dict(row) for row in rows]

    def list_invalid(self) -> list[dict[str, Any]]:
        with self.database.connect() as connection:
            rows = connection.execute(
                """SELECT * FROM scenario_catalog
                   WHERE active=1 AND last_error IS NOT NULL ORDER BY source, scenario_id"""
            ).fetchall()
        result = []
        for row in rows:
            item = dict(row)
            try:
                item["errors"] = json.loads(item["last_error"])
            except (TypeError, json.JSONDecodeError):
                item["errors"] = [{"path": "$", "code": "sync", "message": str(item["last_error"])}]
            result.append(item)
        return result

    def get(self, source: str, scenario_id: str) -> dict[str, Any] | None:
        with self.database.connect() as connection:
            row = connection.execute(
                "SELECT * FROM scenario_catalog WHERE source=? AND scenario_id=?",
                (source, scenario_id),
            ).fetchone()
        return dict(row) if row is not None else None

    def update_sync_status(self, *, source: str, reachable: bool, error: str | None) -> None:
        now = utc_now()
        with self.database.connect() as connection:
            connection.execute(
                """INSERT INTO scenario_sync_state
                   (source, reachable, last_sync_at, last_success_at, last_error)
                   VALUES (?, ?, ?, ?, ?)
                   ON CONFLICT(source) DO UPDATE SET
                     reachable=excluded.reachable,
                     last_sync_at=excluded.last_sync_at,
                     last_success_at=CASE WHEN excluded.reachable=1
                       THEN excluded.last_sync_at ELSE scenario_sync_state.last_success_at END,
                     last_error=excluded.last_error""",
                (source, int(reachable), now, now if reachable else None, error),
            )

    def sync_status(self, source: str) -> dict[str, Any] | None:
        with self.database.connect() as connection:
            row = connection.execute(
                "SELECT * FROM scenario_sync_state WHERE source=?", (source,)
            ).fetchone()
        return dict(row) if row is not None else None
