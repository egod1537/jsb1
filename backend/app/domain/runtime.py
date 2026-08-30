from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class RuntimeRevision:
    """A mutable branch request resolved to an immutable JSB0 commit."""

    repository_id: int
    requested_branch: str | None
    resolved_commit_sha: str


@dataclass(frozen=True)
class BuildKey:
    """The complete identity of a reusable JSB0 build."""

    repository_id: int
    commit_sha: str

