from __future__ import annotations

import asyncio
from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException

from app.api.dependencies import (
    get_scenario_compatibility,
    get_scenario_sync,
    get_scenario_validator,
    get_scenario_writer,
    get_scenarios,
)
from app.domain.scenario import (
    InvalidScenarioEntry,
    ScenarioBatchRequest,
    ScenarioBatchResponse,
    ScenarioBatchResult,
    ScenarioCatalogEntry,
    ScenarioCreateRequest,
    ScenarioCreateResponse,
    ScenarioSyncResult,
    ScenarioSyncStatus,
    ScenarioValidateRequest,
)
from app.domain.scenario_validation import (
    ScenarioValidationPolicy,
    ScenarioValidationResult,
)
from app.services.scenario_sync import (
    ScenarioSyncService,
    StableScenarioContractResolver,
)
from app.services.scenario_validator import ScenarioValidator
from app.services.scenario_writes import (
    ManagedScenarioConflict,
    ManagedScenarioPathError,
    ManagedScenarioValidationFailed,
    ScenarioWriteService,
)
from app.services.scenarios import ScenarioCatalogUnavailable, ScenarioService

router = APIRouter(prefix="/scenarios", tags=["scenarios"])


@router.get("", response_model=list[ScenarioCatalogEntry])
def list_scenarios(service: Annotated[ScenarioService, Depends(get_scenarios)]):
    try:
        return service.catalog()
    except ScenarioCatalogUnavailable as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc


@router.post("", response_model=ScenarioCreateResponse, status_code=201)
async def create_scenario(
    body: ScenarioCreateRequest,
    service: Annotated[ScenarioWriteService, Depends(get_scenario_writer)],
):
    try:
        return await asyncio.to_thread(service.create, body.path, body.yaml)
    except ManagedScenarioConflict as exc:
        raise HTTPException(status_code=409, detail=str(exc)) from exc
    except ManagedScenarioPathError as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    except ManagedScenarioValidationFailed as exc:
        raise HTTPException(
            status_code=422,
            detail={
                "message": str(exc),
                "validation": exc.validation.model_dump(mode="json"),
            },
        ) from exc


@router.get("/invalid", response_model=list[InvalidScenarioEntry])
def invalid_scenarios(service: Annotated[ScenarioService, Depends(get_scenarios)]):
    try:
        return service.invalid_catalog()
    except ScenarioCatalogUnavailable as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc


async def _contract(
    resolver: StableScenarioContractResolver,
    validator: ScenarioValidator,
):
    try:
        compatibility = await asyncio.to_thread(resolver.resolve)
        contract = await asyncio.to_thread(
            validator.load_runtime_contract, compatibility.runtime_root
        )
    except Exception as exc:
        raise HTTPException(
            status_code=503,
            detail="Configured JSB0 scenario contract is unavailable",
        ) from exc
    return compatibility, contract


@router.post("/validate", response_model=ScenarioValidationResult)
async def validate_scenario(
    body: ScenarioValidateRequest,
    validator: Annotated[ScenarioValidator, Depends(get_scenario_validator)],
    resolver: Annotated[
        StableScenarioContractResolver, Depends(get_scenario_compatibility)
    ],
):
    compatibility, contract = await _contract(resolver, validator)
    return validator.validate_yaml(
        body.yaml,
        contract,
        runtime_branch=compatibility.runtime.branch,
        runtime_commit=compatibility.runtime.commit,
        policy=ScenarioValidationPolicy.CATALOG_STABLE,
    )


@router.post("/validate/batch", response_model=ScenarioBatchResponse)
async def validate_scenario_batch(
    body: ScenarioBatchRequest,
    validator: Annotated[ScenarioValidator, Depends(get_scenario_validator)],
    resolver: Annotated[
        StableScenarioContractResolver, Depends(get_scenario_compatibility)
    ],
):
    compatibility, contract = await _contract(resolver, validator)
    validations = validator.validate_many(
        [(item.id, item.yaml) for item in body.scenarios],
        contract,
        runtime_branch=compatibility.runtime.branch,
        runtime_commit=compatibility.runtime.commit,
        policy=ScenarioValidationPolicy.CATALOG_STABLE,
    )
    valid_count = sum(result.valid for _, result in validations)
    return ScenarioBatchResponse(
        runtime_commit=compatibility.runtime.commit,
        total=len(validations),
        valid=valid_count,
        invalid=len(validations) - valid_count,
        results=[
            ScenarioBatchResult(id=scenario_id, validation=result)
            for scenario_id, result in validations
        ],
    )


@router.post("/sync", response_model=ScenarioSyncResult)
async def sync_scenarios(
    service: Annotated[ScenarioSyncService, Depends(get_scenario_sync)],
):
    return await asyncio.to_thread(service.sync)


@router.get("/sync/status", response_model=ScenarioSyncStatus)
def sync_status(service: Annotated[ScenarioSyncService, Depends(get_scenario_sync)]):
    return service.status()
