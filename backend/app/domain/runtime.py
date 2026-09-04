from __future__ import annotations

from dataclasses import dataclass

from app.domain.identifiers import CommitSha, RepositoryId


@dataclass(frozen=True)
class RuntimeRevision:
    """A mutable branch request resolved to an immutable JSB0 commit."""

    repository_id: RepositoryId
    requested_branch: str | None
    resolved_commit_sha: CommitSha


@dataclass(frozen=True)
class BuildKey:
    """The complete identity of a reusable JSB0 build."""

    repository_id: RepositoryId
    commit_sha: CommitSha
