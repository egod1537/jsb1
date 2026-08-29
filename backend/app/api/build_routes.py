from __future__ import annotations

import asyncio
from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, Query
from fastapi.responses import FileResponse

from app.api.dependencies import (
    get_build_manager,
    get_build_scheduler,
    get_builds,
)
from app.domain.build import Build, BuildCreate
from app.repositories.builds import BuildRepository
from app.services.build_manager import BuildManager
from app.services.repository_manager import GitOperationError, InvalidRepositoryPath
from app.workers.build_scheduler import InProcessBuildScheduler


router = APIRouter(prefix="/builds", tags=["builds"])


def _build_error(exc: Exception) -> HTTPException:
    if isinstance(exc, KeyError):
        return HTTPException(status_code=404, detail="repository or build not found")
    return HTTPException(status_code=422, detail=str(exc))


@router.get("", response_model=list[Build])
def list_builds(
    builds: Annotated[BuildRepository, Depends(get_builds)],
    repository_id: int | None = None,
    limit: Annotated[int, Query(ge=1, le=200)] = 100,
) -> list[Build]:
    return builds.list(repository_id=repository_id, limit=limit)


@router.post("", response_model=Build, status_code=202)
async def create_build(
    body: BuildCreate,
    manager: Annotated[BuildManager, Depends(get_build_manager)],
    scheduler: Annotated[InProcessBuildScheduler, Depends(get_build_scheduler)],
) -> Build:
    try:
        build, reused = await asyncio.to_thread(
            manager.request,
            body.repository_id,
            body.revision,
            rebuild=body.rebuild,
        )
        if not reused:
            scheduler.submit(build.id)
        return build
    except (KeyError, GitOperationError, InvalidRepositoryPath, RuntimeError) as exc:
        raise _build_error(exc) from exc


@router.get("/{build_id}", response_model=Build)
def build_detail(
    build_id: int,
    builds: Annotated[BuildRepository, Depends(get_builds)],
) -> Build:
    try:
        return builds.get(build_id)
    except KeyError as exc:
        raise HTTPException(status_code=404, detail="build not found") from exc


@router.post("/{build_id}/rebuild", response_model=Build, status_code=202)
async def rebuild(
    build_id: int,
    builds: Annotated[BuildRepository, Depends(get_builds)],
    manager: Annotated[BuildManager, Depends(get_build_manager)],
    scheduler: Annotated[InProcessBuildScheduler, Depends(get_build_scheduler)],
) -> Build:
    try:
        previous = builds.get(build_id)
        build, _ = await asyncio.to_thread(
            manager.request,
            previous.repository_id,
            previous.commit_sha,
            rebuild=True,
        )
        scheduler.submit(build.id)
        return build
    except (KeyError, GitOperationError, InvalidRepositoryPath, RuntimeError) as exc:
        raise _build_error(exc) from exc


@router.get("/{build_id}/logs/{stream}")
def build_log(
    build_id: int,
    stream: str,
    manager: Annotated[BuildManager, Depends(get_build_manager)],
) -> FileResponse:
    try:
        path = manager.log_path(build_id, stream)
    except (KeyError, FileNotFoundError, InvalidRepositoryPath) as exc:
        raise HTTPException(status_code=404, detail="build log not found")
    return FileResponse(path, filename=f"build-{build_id}-{stream}.log", media_type="text/plain")
