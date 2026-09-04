from __future__ import annotations

import re
from urllib.parse import urlsplit

from app.infrastructure.git.repository import GitOperationError


class GitReferencePolicy:
    """Validate Git-facing identifiers without coupling services to Git syntax."""

    @staticmethod
    def normalize_remote_url(value: str) -> str:
        cleaned = value.strip().rstrip("/")
        github = re.fullmatch(
            r"(?:https?://github\.com/|ssh://git@github\.com/|git@github\.com:)"
            r"([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+?)(?:\.git)?",
            cleaned,
            flags=re.IGNORECASE,
        )
        if github:
            owner, repository = github.groups()
            return f"https://github.com/{owner}/{repository}.git"
        return cleaned

    @classmethod
    def display_name(cls, remote_url: str, *, fallback: str) -> str:
        normalized = cls.normalize_remote_url(remote_url)
        github = re.fullmatch(
            r"https://github\.com/([^/]+)/([^/]+?)(?:\.git)?", normalized
        )
        return f"{github.group(1)}/{github.group(2)}" if github else fallback

    @staticmethod
    def validate_remote_url(value: str) -> None:
        parsed = urlsplit(value)
        safe_scheme = (
            parsed.scheme in {"https", "http", "ssh", "git"}
            and bool(parsed.hostname)
            and bool(parsed.path.strip("/"))
        )
        safe_scp = re.fullmatch(
            r"[A-Za-z0-9._-]+@[A-Za-z0-9.-]+:"
            r"[A-Za-z0-9._/~+-]+(?:/[A-Za-z0-9._~+-]+)*",
            value,
        )
        if not safe_scheme and safe_scp is None:
            raise GitOperationError(
                "remote_url must use http(s), ssh, git, or scp syntax"
            )

    @staticmethod
    def validate_branch_name(value: str) -> None:
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._/-]{0,254}", value):
            raise GitOperationError("invalid branch name")
        if (
            value.endswith(("/", ".", ".lock"))
            or ".." in value
            or "//" in value
            or "@{" in value
            or "/." in value
        ):
            raise GitOperationError("invalid branch name")
