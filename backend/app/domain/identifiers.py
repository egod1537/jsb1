from __future__ import annotations

from typing import NewType

# Lightweight nominal types: they improve service/repository signatures without
# adding runtime wrappers or changing JSON and SQLite representations.
RunId = NewType("RunId", int)
BuildId = NewType("BuildId", int)
RepositoryId = NewType("RepositoryId", int)
ComparisonId = NewType("ComparisonId", int)
CommitSha = NewType("CommitSha", str)
ScenarioId = NewType("ScenarioId", str)


def commit_sha(value: str) -> CommitSha:
    normalized = value.lower()
    if len(normalized) != 40 or any(
        character not in "0123456789abcdef" for character in normalized
    ):
        raise ValueError("commit SHA must contain exactly 40 hexadecimal characters")
    return CommitSha(normalized)
