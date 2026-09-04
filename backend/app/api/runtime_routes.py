from __future__ import annotations

import asyncio
from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, Query

from app.api.dependencies import (
    get_controller_parameters,
    get_repository_manager,
    get_runtime_variants,
)
from app.domain.models import (
    RuntimeControllerParametersResponse,
    RuntimeVariantsResponse,
)
from app.domain.repository import Branch, RuntimeRepositoryStatus
from app.services.controller_parameters import (
    ControllerParameterError,
    RuntimeControllerParameterService,
)
from app.services.repository_manager import (
    GitOperationError,
    InvalidRepositoryPath,
    RepositoryManager,
    RuntimeRepositoryNotConfigured,
    RuntimeRepositoryUnavailable,
)
from app.services.runtime_contract import RuntimeContractError
from app.services.runtime_variants import (
    RuntimeVariantContractError,
    RuntimeVariantService,
)

router = APIRouter(prefix="/runtime", tags=["runtime"])


@router.get("/repository", response_model=RuntimeRepositoryStatus)
def runtime_repository(
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
) -> RuntimeRepositoryStatus:
    try:
        return manager.runtime_status()
    except RuntimeRepositoryNotConfigured as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except RuntimeRepositoryUnavailable as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc


@router.get("/parameters", response_model=RuntimeControllerParametersResponse)
async def runtime_parameters(
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
    service: Annotated[
        RuntimeControllerParameterService, Depends(get_controller_parameters)
    ],
    branch: Annotated[str | None, Query(min_length=1, max_length=255)] = None,
    commit_sha: Annotated[
        str | None, Query(pattern="^[0-9a-fA-F]{40}$")
    ] = None,
) -> RuntimeControllerParametersResponse:
    try:
        runtime = await asyncio.to_thread(manager.runtime_repository)
        selected_branch = branch or runtime.default_branch
        await asyncio.to_thread(manager.fetch, runtime.id)
        revision = await asyncio.to_thread(
            manager.revision if commit_sha else manager.resolve_branch,
            runtime.id,
            commit_sha or selected_branch,
        )
        worktree = await asyncio.to_thread(
            manager.prepare_worktree, runtime.id, revision.commit_sha
        )
        if service.reader.is_indexed(worktree):
            await asyncio.to_thread(
                service.reader.load_bundle,
                worktree,
                repository_id=runtime.id,
                commit_sha=revision.commit_sha,
            )
        catalog = await asyncio.to_thread(service.catalog, worktree)
    except (RuntimeRepositoryNotConfigured, RuntimeRepositoryUnavailable) as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except (KeyError, GitOperationError, InvalidRepositoryPath) as exc:
        raise HTTPException(status_code=422, detail="Could not resolve JSB0 branch") from exc
    except (ControllerParameterError, RuntimeContractError) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    return RuntimeControllerParametersResponse(
        branch=selected_branch,
        commit_sha=revision.commit_sha,
        source=catalog.source,
        transport=catalog.transport,
        parameters=list(catalog.parameters),
    )


@router.post("/repository/fetch", response_model=RuntimeRepositoryStatus)
def fetch_runtime_repository(
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
) -> RuntimeRepositoryStatus:
    try:
        return manager.fetch_runtime_repository()
    except RuntimeRepositoryNotConfigured as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except RuntimeRepositoryUnavailable as exc:
        raise HTTPException(status_code=502, detail=str(exc)) from exc


@router.get("/branches", response_model=list[Branch])
def runtime_branches(
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
) -> list[Branch]:
    try:
        return manager.runtime_branches()
    except (RuntimeRepositoryNotConfigured, RuntimeRepositoryUnavailable) as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except (KeyError, GitOperationError, InvalidRepositoryPath) as exc:
        raise HTTPException(
            status_code=502, detail="Could not refresh JSB0 branches"
        ) from exc


@router.get("/variants", response_model=RuntimeVariantsResponse)
async def runtime_variants(
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
    variants: Annotated[RuntimeVariantService, Depends(get_runtime_variants)],
    branch: Annotated[str | None, Query(min_length=1, max_length=255)] = None,
) -> RuntimeVariantsResponse:
    try:
        runtime = await asyncio.to_thread(manager.runtime_repository)
        selected_branch = branch or runtime.default_branch
        await asyncio.to_thread(manager.fetch, runtime.id)
        revision = await asyncio.to_thread(
            manager.resolve_branch, runtime.id, selected_branch
        )
        worktree = await asyncio.to_thread(
            manager.prepare_worktree, runtime.id, revision.commit_sha
        )
        if variants.reader.is_indexed(worktree):
            bundle = await asyncio.to_thread(
                variants.reader.load_bundle,
                worktree,
                repository_id=runtime.id,
                commit_sha=revision.commit_sha,
            )
            capability = bundle.capabilities
        else:
            capability = await asyncio.to_thread(variants.capabilities, worktree)
    except (RuntimeRepositoryNotConfigured, RuntimeRepositoryUnavailable) as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except (KeyError, GitOperationError, InvalidRepositoryPath) as exc:
        raise HTTPException(status_code=422, detail="Could not resolve JSB0 branch") from exc
    except RuntimeVariantContractError as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    return RuntimeVariantsResponse(
        branch=selected_branch,
        commit_sha=revision.commit_sha,
        mode=capability.mode,
        variants=list(capability.variants),
    )
