from __future__ import annotations

from collections.abc import Mapping
from enum import Enum
from typing import TypeVar

from app.domain.errors import InvalidStatusTransition, InvalidTransition

Status = TypeVar("Status", bound=Enum)


def ensure_transition(
    current: Status,
    target: Status,
    allowed: Mapping[Status, frozenset[Status]],
    *,
    entity: str,
    error_type: type[InvalidTransition] = InvalidStatusTransition,
) -> None:
    """Reject lifecycle edges not declared by the domain state machine."""
    if target not in allowed.get(current, frozenset()):
        raise error_type(
            f"invalid {entity} transition from {current.value} to {target.value}"
        )
