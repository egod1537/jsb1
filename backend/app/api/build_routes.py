from __future__ import annotations

from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, Query
from fastapi.responses import FileResponse

from app.api.dependencies import (
    get_build_requests,
)
from app.domain.build import Build, BuildCreate
from app.services.build_requests import BuildRequestService
from app.services.repository_manager import GitOperationError, InvalidRepositoryPath

router = APIRouter(prefix="/builds", tags=["builds"])


def _build_error(exc: Exception) -> HTTPException:
    if isinstance(exc, KeyError):
        return HTTPException(status_code=404, detail="repository or build not found")
    return HTTPException(status_code=422, detail=str(exc))


@router.get("", response_model=list[Build])
def list_builds(
    service: Annotated[BuildRequestService, Depends(get_build_requests)],
    repository_id: int | None = None,
    limit: Annotated[int, Query(ge=1, le=200)] = 100,
) -> list[Build]:
    return service.list(repository_id=repository_id, limit=limit)


@router.post("", response_model=Build, status_code=202)
async def create_build(
    body: BuildCreate,
    service: Annotated[BuildRequestService, Depends(get_build_requests)],
) -> Build:
    try:
        return await service.request(
            body.repository_id, body.revision, rebuild=body.rebuild
        )
    except (KeyError, GitOperationError, InvalidRepositoryPath, RuntimeError) as exc:
        raise _build_error(exc) from exc


@router.get("/{build_id}", response_model=Build)
def build_detail(
    build_id: int,
    service: Annotated[BuildRequestService, Depends(get_build_requests)],
) -> Build:
    try:
        return service.get(build_id)
    except KeyError as exc:
        raise HTTPException(status_code=404, detail="build not found") from exc


@router.post("/{build_id}/rebuild", response_model=Build, status_code=202)
async def rebuild(
    build_id: int,
    service: Annotated[BuildRequestService, Depends(get_build_requests)],
) -> Build:
    try:
        return await service.rebuild(build_id)
    except (KeyError, GitOperationError, InvalidRepositoryPath, RuntimeError) as exc:
        raise _build_error(exc) from exc


@router.get("/{build_id}/logs/{stream}")
def build_log(
    build_id: int,
    stream: str,
    service: Annotated[BuildRequestService, Depends(get_build_requests)],
) -> FileResponse:
    try:
        path = service.log_path(build_id, stream)
    except (KeyError, FileNotFoundError, InvalidRepositoryPath):
        raise HTTPException(status_code=404, detail="build log not found")
    return FileResponse(path, filename=f"build-{build_id}-{stream}.log", media_type="text/plain")
