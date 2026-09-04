from __future__ import annotations

from typing import Annotated, Literal

from fastapi import APIRouter, Depends, HTTPException, Query

from app.api.dependencies import (
    get_scenario_inspection_use_cases,
)
from app.domain.scenario import ScenarioInspectionCatalogEntry, ScenarioInspectionDetail
from app.services.scenario_inspection_use_cases import (
    ScenarioContractUnavailable,
    ScenarioInspectionUseCases,
)
from app.services.scenario_snapshot_queries import (
    ComparisonSnapshotOwnerNotFound,
    ScenarioSnapshotNotFound,
)
from app.services.scenarios import InvalidScenario

router = APIRouter(tags=["scenario inspection"])


@router.get(
    "/scenario-catalog",
    response_model=list[ScenarioInspectionCatalogEntry],
)
async def scenario_catalog(
    service: Annotated[
        ScenarioInspectionUseCases, Depends(get_scenario_inspection_use_cases)
    ],
):
    try:
        return await service.catalog()
    except ScenarioContractUnavailable as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc


@router.get("/scenario-catalog/detail", response_model=ScenarioInspectionDetail)
async def scenario_detail(
    service: Annotated[
        ScenarioInspectionUseCases, Depends(get_scenario_inspection_use_cases)
    ],
    source: Annotated[Literal["bundled", "managed", "sftp"], Query()],
    scenario_id: Annotated[str, Query(alias="id", min_length=1, max_length=1024)],
):
    try:
        return await service.detail(source, scenario_id)
    except ScenarioContractUnavailable as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except InvalidScenario as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc


@router.get("/runs/{run_id}/scenario", response_model=ScenarioInspectionDetail)
async def run_scenario_snapshot(
    run_id: int,
    service: Annotated[
        ScenarioInspectionUseCases, Depends(get_scenario_inspection_use_cases)
    ],
):
    try:
        return await service.run_snapshot(run_id)
    except ScenarioSnapshotNotFound as exc:
        raise HTTPException(status_code=404, detail="run scenario snapshot not found") from exc


@router.get(
    "/comparisons/{comparison_id}/scenario",
    response_model=ScenarioInspectionDetail,
)
async def comparison_scenario_snapshot(
    comparison_id: int,
    service: Annotated[
        ScenarioInspectionUseCases, Depends(get_scenario_inspection_use_cases)
    ],
):
    try:
        return await service.comparison_snapshot(comparison_id)
    except ComparisonSnapshotOwnerNotFound as exc:
        raise HTTPException(status_code=404, detail="comparison not found") from exc
    except ScenarioSnapshotNotFound as exc:
        raise HTTPException(
            status_code=404, detail="comparison scenario snapshot not found"
        ) from exc
