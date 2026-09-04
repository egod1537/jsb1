from __future__ import annotations

from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, Response

from app.api.dependencies import get_repository_manager
from app.domain.errors import RepositoryConflict
from app.domain.repository import Branch, RepositoryCreate, RepositoryStatus, Revision
from app.services.repository_manager import (
    GitOperationError,
    InvalidRepositoryPath,
    RepositoryManager,
)

router = APIRouter(prefix="/repositories", tags=["repositories"])


def _not_found_or_invalid(exc: Exception) -> HTTPException:
    if isinstance(exc, KeyError):
        return HTTPException(status_code=404, detail="repository not found")
    if isinstance(exc, RepositoryConflict):
        return HTTPException(status_code=409, detail=str(exc))
    return HTTPException(status_code=422, detail=str(exc))


@router.get("", response_model=list[RepositoryStatus])
def list_repositories(
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
) -> list[RepositoryStatus]:
    return manager.list_status()


@router.post("", response_model=RepositoryStatus, status_code=201, deprecated=True)
def create_repository(
    body: RepositoryCreate,
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
) -> RepositoryStatus:
    try:
        return manager.register(body)
    except (KeyError, RepositoryConflict, GitOperationError, InvalidRepositoryPath) as exc:
        raise _not_found_or_invalid(exc) from exc


@router.get("/{repository_id}", response_model=RepositoryStatus)
def repository_detail(
    repository_id: int,
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
) -> RepositoryStatus:
    try:
        return manager.status(repository_id)
    except (KeyError, GitOperationError, InvalidRepositoryPath) as exc:
        raise _not_found_or_invalid(exc) from exc


@router.delete("/{repository_id}", status_code=204, deprecated=True)
def delete_repository(
    repository_id: int,
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
) -> Response:
    try:
        manager.delete(repository_id)
    except (KeyError, RepositoryConflict) as exc:
        raise _not_found_or_invalid(exc) from exc
    return Response(status_code=204)


@router.post("/{repository_id}/fetch", response_model=RepositoryStatus)
def fetch_repository(
    repository_id: int,
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
) -> RepositoryStatus:
    try:
        return manager.fetch(repository_id)
    except (KeyError, GitOperationError, InvalidRepositoryPath) as exc:
        raise _not_found_or_invalid(exc) from exc


@router.get("/{repository_id}/branches", response_model=list[Branch])
def repository_branches(
    repository_id: int,
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
) -> list[Branch]:
    try:
        return manager.branches(repository_id)
    except (KeyError, GitOperationError, InvalidRepositoryPath) as exc:
        raise _not_found_or_invalid(exc) from exc


@router.get("/{repository_id}/revisions/{revision:path}", response_model=Revision)
def repository_revision(
    repository_id: int,
    revision: str,
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
) -> Revision:
    try:
        return manager.revision(repository_id, revision)
    except (KeyError, GitOperationError, InvalidRepositoryPath) as exc:
        raise _not_found_or_invalid(exc) from exc
