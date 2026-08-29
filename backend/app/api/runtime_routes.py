from __future__ import annotations

from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException

from app.api.dependencies import get_app_settings, get_repository_manager
from app.config.settings import Settings
from app.domain.repository import Branch, RepositoryStatus
from app.services.repository_manager import (
    GitOperationError,
    InvalidRepositoryPath,
    RepositoryManager,
    RuntimeRepositoryNotConfigured,
)


router = APIRouter(prefix="/runtime", tags=["runtime"])


def _runtime_repository(
    manager: RepositoryManager, settings: Settings
) -> RepositoryStatus:
    try:
        return manager.runtime_repository(settings.runtime_repository_name)
    except RuntimeRepositoryNotConfigured as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except (GitOperationError, InvalidRepositoryPath) as exc:
        raise HTTPException(
            status_code=503, detail="JSB0 Runtime repository is not configured"
        ) from exc


@router.get("/repository", response_model=RepositoryStatus)
def runtime_repository(
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
    settings: Annotated[Settings, Depends(get_app_settings)],
) -> RepositoryStatus:
    return _runtime_repository(manager, settings)


@router.get("/branches", response_model=list[Branch])
def runtime_branches(
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
    settings: Annotated[Settings, Depends(get_app_settings)],
) -> list[Branch]:
    repository = _runtime_repository(manager, settings)
    try:
        return manager.branches(repository.id)
    except (KeyError, GitOperationError, InvalidRepositoryPath) as exc:
        raise HTTPException(
            status_code=502, detail="Could not refresh JSB0 branches"
        ) from exc
