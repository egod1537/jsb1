from __future__ import annotations

from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, Query, Response

from app.api.dependencies import get_deployment_manager, get_deployments
from app.domain.deployment import BranchDeployment, DeploymentCreate
from app.repositories.deployments import (
    DeploymentConflict,
    DeploymentRepository,
    InvalidDeploymentTransition,
    NoDeploymentPortAvailable,
)
from app.services.deployment_manager import (
    DeploymentConfigurationError,
    DeploymentManager,
    DeploymentOperationError,
)
from app.services.repository_manager import GitOperationError, InvalidRepositoryPath


router = APIRouter(prefix="/deployments", tags=["deployments"])


def _deployment_error(exc: Exception) -> HTTPException:
    if isinstance(exc, KeyError):
        return HTTPException(status_code=404, detail="deployment or repository not found")
    if isinstance(exc, (DeploymentConflict, InvalidDeploymentTransition)):
        return HTTPException(status_code=409, detail=str(exc))
    if isinstance(exc, DeploymentConfigurationError):
        return HTTPException(status_code=503, detail=str(exc))
    return HTTPException(status_code=422, detail=str(exc))


@router.get("", response_model=list[BranchDeployment])
def list_deployments(
    deployments: Annotated[DeploymentRepository, Depends(get_deployments)],
    repository_id: int | None = None,
    limit: Annotated[int, Query(ge=1, le=500)] = 200,
) -> list[BranchDeployment]:
    return deployments.list(repository_id=repository_id, limit=limit)


@router.post("", response_model=BranchDeployment, status_code=202)
async def create_deployment(
    body: DeploymentCreate,
    manager: Annotated[DeploymentManager, Depends(get_deployment_manager)],
) -> BranchDeployment:
    try:
        return await manager.submit(body.repository_id, body.branch)
    except (
        KeyError,
        GitOperationError,
        InvalidRepositoryPath,
        DeploymentConflict,
        DeploymentOperationError,
    ) as exc:
        raise _deployment_error(exc) from exc


@router.get("/{deployment_id}", response_model=BranchDeployment)
def deployment_detail(
    deployment_id: int,
    manager: Annotated[DeploymentManager, Depends(get_deployment_manager)],
) -> BranchDeployment:
    try:
        return manager.status(deployment_id)
    except KeyError as exc:
        raise _deployment_error(exc) from exc


@router.post("/{deployment_id}/redeploy", response_model=BranchDeployment, status_code=202)
async def redeploy(
    deployment_id: int,
    manager: Annotated[DeploymentManager, Depends(get_deployment_manager)],
) -> BranchDeployment:
    try:
        current = manager.status(deployment_id)
        return await manager.submit(current.repository_id, current.branch)
    except (
        KeyError,
        GitOperationError,
        InvalidRepositoryPath,
        DeploymentConflict,
        DeploymentOperationError,
    ) as exc:
        raise _deployment_error(exc) from exc


@router.post("/{deployment_id}/restart", response_model=BranchDeployment)
async def restart_deployment(
    deployment_id: int,
    manager: Annotated[DeploymentManager, Depends(get_deployment_manager)],
) -> BranchDeployment:
    try:
        return await manager.restart(deployment_id)
    except (
        KeyError,
        InvalidDeploymentTransition,
        DeploymentConfigurationError,
        DeploymentOperationError,
    ) as exc:
        raise _deployment_error(exc) from exc


@router.delete("/{deployment_id}", status_code=204)
async def stop_deployment(
    deployment_id: int,
    manager: Annotated[DeploymentManager, Depends(get_deployment_manager)],
    force: bool = False,
) -> Response:
    try:
        await manager.stop(deployment_id, force=force)
    except (
        KeyError,
        InvalidDeploymentTransition,
        DeploymentOperationError,
    ) as exc:
        raise _deployment_error(exc) from exc
    return Response(status_code=204)
