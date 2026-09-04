from __future__ import annotations

import pytest
from app.domain.errors import (
    Conflict,
    ContractCompatibilityError,
    InvalidTransition,
    RepositoryConflict,
)
from app.domain.errors import (
    InvalidStatusTransition as InvalidBuildTransition,
)
from app.domain.identifiers import CommitSha, commit_sha
from app.services.runtime_contract import UnsupportedRuntimeContractVersion


def test_commit_sha_normalizes_a_complete_hex_object_id() -> None:
    value = commit_sha("A" * 40)

    assert value == "a" * 40
    assert isinstance(value, str)
    assert CommitSha(value) == value


@pytest.mark.parametrize("value", ["", "abc", "g" * 40, "a" * 39, "a" * 41])
def test_commit_sha_rejects_non_object_ids(value: str) -> None:
    with pytest.raises(ValueError, match="40 hexadecimal"):
        commit_sha(value)


def test_specific_failures_have_stable_boundary_categories() -> None:
    assert issubclass(RepositoryConflict, Conflict)
    assert issubclass(InvalidBuildTransition, InvalidTransition)
    assert issubclass(
        UnsupportedRuntimeContractVersion, ContractCompatibilityError
    )
