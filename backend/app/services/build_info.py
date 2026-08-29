from __future__ import annotations

from app.config.settings import Settings
from app.domain.build_info import BuildInfo


def _metadata_value(value: str | None, fallback: str) -> str:
    if value is None:
        return fallback
    normalized = value.strip()
    return normalized or fallback


def load_build_info(settings: Settings) -> BuildInfo:
    branch = _metadata_value(settings.build_branch, "dev")
    commit = _metadata_value(settings.build_commit, "unknown")
    built_at = _metadata_value(settings.build_time, "unknown")
    hostname = _metadata_value(settings.build_hostname, "") or None
    short_commit = commit[:7] if commit != "unknown" else "unknown"
    return BuildInfo(
        branch=branch,
        commit=commit,
        short_commit=short_commit,
        built_at=built_at,
        hostname=hostname,
    )
