from __future__ import annotations

from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, Query

from app.api.dependencies import (
    get_comparison_creation,
    get_comparison_queries,
)
from app.domain.errors import RuntimeRevisionNotFound, SnapshotWriteFailed
from app.domain.models import Comparison, ComparisonCreate
from app.services.comparison_creation import (
    ComparisonCreationService,
    CreateComparisonCommand,
)
from app.services.comparison_queries import ComparisonNotFound, ComparisonQueryService
from app.services.repository_manager import (
    GitOperationError,
    InvalidRepositoryPath,
    RuntimeRepositoryNotConfigured,
    RuntimeRepositoryUnavailable,
)
from app.services.runtime_variants import RuntimeVariantContractError
from app.services.scenarios import InvalidScenario

router = APIRouter(prefix="/comparisons", tags=["comparisons"])


@router.post("", response_model=Comparison, status_code=202)
async def create_comparison(
    body: ComparisonCreate,
    service: Annotated[ComparisonCreationService, Depends(get_comparison_creation)],
) -> Comparison:
    try:
        return await service.create(
            CreateComparisonCommand(
                scenario=body.scenario,
                scenario_source=body.scenario_source,
                branch=body.branch,
                variants=tuple(body.variants),
            )
        )
    except InvalidScenario as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    except RuntimeRepositoryNotConfigured as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except RuntimeRepositoryUnavailable as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except RuntimeVariantContractError as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    except SnapshotWriteFailed as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc
    except RuntimeRevisionNotFound as exc:
        raise HTTPException(status_code=422, detail="Could not resolve JSB0 branch") from exc
    except (KeyError, GitOperationError, InvalidRepositoryPath, OSError) as exc:
        raise HTTPException(status_code=422, detail="Could not resolve JSB0 branch") from exc

    except (ValueError, RuntimeError) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc


@router.get("", response_model=list[Comparison])
def list_comparisons(
    queries: Annotated[ComparisonQueryService, Depends(get_comparison_queries)],
    limit: Annotated[int, Query(ge=1, le=200)] = 50,
) -> list[Comparison]:
    return queries.list(limit)


@router.get("/{comparison_id}", response_model=Comparison)
def comparison_detail(
    comparison_id: int,
    queries: Annotated[ComparisonQueryService, Depends(get_comparison_queries)],
) -> Comparison:
    try:
        return queries.require(comparison_id)
    except ComparisonNotFound as exc:
        raise HTTPException(status_code=404, detail="comparison not found") from exc
