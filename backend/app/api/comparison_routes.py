from __future__ import annotations

from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, Query

from app.api.dependencies import (
    get_comparison_creation,
    get_comparisons,
)
from app.domain.models import Comparison, ComparisonCreate
from app.domain.errors import SnapshotWriteFailed
from app.repositories.comparisons import ComparisonRepository
from app.services.repository_manager import (
    GitOperationError,
    InvalidRepositoryPath,
    RuntimeRepositoryNotConfigured,
    RuntimeRepositoryUnavailable,
)
from app.services.runtime_variants import RuntimeVariantContractError
from app.services.scenarios import InvalidScenario
from app.services.comparison_creation import (
    ComparisonCreationService,
    CreateComparisonCommand,
)


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
    except (KeyError, GitOperationError, InvalidRepositoryPath, OSError) as exc:
        detail = "Branch no longer exists" if str(exc).startswith("branch not found:") else "Could not resolve JSB0 branch"
        raise HTTPException(status_code=422, detail=detail) from exc

    except (ValueError, RuntimeError) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc


@router.get("", response_model=list[Comparison])
def list_comparisons(
    comparisons: Annotated[ComparisonRepository, Depends(get_comparisons)],
    limit: Annotated[int, Query(ge=1, le=200)] = 50,
) -> list[Comparison]:
    return comparisons.list(limit)


@router.get("/{comparison_id}", response_model=Comparison)
def comparison_detail(
    comparison_id: int,
    comparisons: Annotated[ComparisonRepository, Depends(get_comparisons)],
) -> Comparison:
    try:
        return comparisons.get(comparison_id)
    except KeyError as exc:
        raise HTTPException(status_code=404, detail="comparison not found") from exc
