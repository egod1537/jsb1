from __future__ import annotations

from typing import Annotated, Any

from fastapi import APIRouter, Depends, HTTPException, Query, Response
from fastapi.responses import FileResponse

from app.analysis.mcap_reader import McapReadError
from app.analysis.roll_hold_analyzer import RollHoldAnalysisVariants
from app.api.build_routes import router as build_router
from app.api.comparison_routes import router as comparison_router
from app.api.dependencies import (
    get_build_info,
    get_health,
    get_run_analysis,
    get_run_creation,
    get_run_deletion,
    get_run_queries,
    get_telemetry_queries,
)
from app.api.deployment_routes import router as deployment_router
from app.api.repository_routes import router as repository_router
from app.api.runtime_routes import router as runtime_router
from app.api.scenario_inspection_routes import router as scenario_inspection_router
from app.api.scenario_routes import router as scenario_router
from app.domain.build_info import BuildInfo
from app.domain.errors import RuntimeRevisionNotFound, SnapshotWriteFailed
from app.domain.models import (
    AvailableSignalsResponse,
    RunCreate,
    RunDetail,
    RunStatus,
    SignalResponse,
)
from app.services.artifacts import UnsafeArtifactPath
from app.services.health import HealthService
from app.services.repository_manager import (
    GitOperationError,
    InvalidRepositoryPath,
    RuntimeRepositoryNotConfigured,
    RuntimeRepositoryUnavailable,
)
from app.services.run_analysis import AnalyzerNotApplicable, RunAnalysisService
from app.services.run_creation import CreateRunCommand, RunCreationService
from app.services.run_deletion import (
    ActiveRunDeletionNotAllowed,
    RunDeletionService,
    UnsafeRunDirectory,
)
from app.services.run_queries import RunNotFound, RunQueryService
from app.services.runtime_variants import RuntimeVariantContractError
from app.services.scenarios import InvalidScenario
from app.services.telemetry_queries import TelemetryQueryService

router = APIRouter(prefix="/api")
router.include_router(repository_router)
router.include_router(runtime_router)
router.include_router(build_router)
router.include_router(deployment_router)
router.include_router(scenario_router)
router.include_router(scenario_inspection_router)
router.include_router(comparison_router)


@router.get("/health")
def health(
    service: Annotated[HealthService, Depends(get_health)],
) -> dict[str, Any]:
    return service.status()


@router.get("/version", response_model=BuildInfo)
def version(build_info: Annotated[BuildInfo, Depends(get_build_info)]) -> BuildInfo:
    return build_info


@router.post("/runs", status_code=202)
async def create_run(
    body: RunCreate,
    service: Annotated[RunCreationService, Depends(get_run_creation)],
) -> dict[str, Any]:
    try:
        result = await service.create(
            CreateRunCommand(
                scenario=body.scenario,
                scenario_source=body.scenario_source,
                variant=body.variant,
                legacy_autopilot=body.autopilot,
                repository_id=body.repository_id,
                branch=body.branch,
                commit_sha=body.commit_sha,
                build_id=body.build_id,
                controller_parameters=body.controller_parameters,
            )
        )
        return result.__dict__
    except RuntimeRepositoryNotConfigured as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except RuntimeRepositoryUnavailable as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except SnapshotWriteFailed as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc
    except RuntimeRevisionNotFound as exc:
        raise HTTPException(status_code=404, detail="Branch no longer exists") from exc
    except GitOperationError as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    except KeyError as exc:
        raise HTTPException(status_code=404, detail="repository or build not found") from exc
    except (InvalidScenario, RuntimeVariantContractError, InvalidRepositoryPath, ValueError, RuntimeError, OSError) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc


@router.get("/runs")
def list_runs(
    queries: Annotated[RunQueryService, Depends(get_run_queries)],
    status: RunStatus | None = None,
    scenario: str | None = None,
    limit: Annotated[int, Query(ge=1, le=200)] = 50,
):
    return queries.list(status=status, scenario=scenario, limit=limit)


@router.get("/runs/{run_id}", response_model=RunDetail)
def run_detail(
    run_id: int,
    queries: Annotated[RunQueryService, Depends(get_run_queries)],
) -> RunDetail:
    try:
        return queries.detail(run_id)
    except RunNotFound as exc:
        raise HTTPException(status_code=404, detail="run not found") from exc


@router.delete("/runs/{run_id}", status_code=204)
def delete_run(
    run_id: int,
    service: Annotated[RunDeletionService, Depends(get_run_deletion)],
) -> Response:
    try:
        service.delete(run_id)
    except KeyError as exc:
        raise HTTPException(status_code=404, detail="run not found") from exc
    except ActiveRunDeletionNotAllowed as exc:
        raise HTTPException(status_code=409, detail=str(exc)) from exc
    except UnsafeRunDirectory as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    return Response(status_code=204)


@router.get("/runs/{run_id}/metrics")
def run_metrics(
    run_id: int,
    queries: Annotated[RunQueryService, Depends(get_run_queries)],
) -> dict[str, float | None]:
    try:
        return queries.metrics(run_id)
    except RunNotFound as exc:
        raise HTTPException(status_code=404, detail="run not found") from exc


@router.get(
    "/runs/{run_id}/analysis/roll-hold",
    response_model=RollHoldAnalysisVariants,
)
def run_roll_hold_analysis(
    run_id: int,
    service: Annotated[RunAnalysisService, Depends(get_run_analysis)],
) -> RollHoldAnalysisVariants:
    try:
        return service.analyze_roll_hold(run_id)
    except KeyError as exc:
        raise HTTPException(status_code=404, detail="run or telemetry artifact not found") from exc
    except AnalyzerNotApplicable as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    except (OSError, ValueError, McapReadError, UnsafeArtifactPath) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc


@router.get("/runs/{run_id}/signals", response_model=SignalResponse)
def run_signals(
    run_id: int,
    queries: Annotated[RunQueryService, Depends(get_run_queries)],
    telemetry: Annotated[TelemetryQueryService, Depends(get_telemetry_queries)],
    signals: Annotated[str | None, Query(min_length=1)] = None,
    channels: Annotated[str | None, Query(min_length=1, deprecated=True)] = None,
    variant: Annotated[str | None, Query(min_length=1, max_length=64)] = None,
    start: Annotated[float | None, Query(ge=0)] = None,
    end: Annotated[float | None, Query(ge=0)] = None,
    max_points: Annotated[int, Query(ge=10, le=20_000)] = 2000,
) -> SignalResponse:
    try:
        queries.require(run_id)
        requested = signals or channels
        if requested is None:
            raise ValueError("signals is required")
        return telemetry.signals(
            run_id,
            requested,
            variant=variant,
            start=start,
            end=end,
            max_points=max_points,
        )
    except RunNotFound as exc:
        raise HTTPException(status_code=404, detail="run not found") from exc
    except KeyError as exc:
        raise HTTPException(status_code=404, detail="telemetry artifact not found") from exc
    except (McapReadError, UnsafeArtifactPath, ValueError) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc


@router.get(
    "/runs/{run_id}/signals/available",
    response_model=AvailableSignalsResponse,
    response_model_exclude_none=True,
)
def available_run_signals(
    run_id: int,
    queries: Annotated[RunQueryService, Depends(get_run_queries)],
    telemetry: Annotated[TelemetryQueryService, Depends(get_telemetry_queries)],
) -> AvailableSignalsResponse:
    try:
        queries.require(run_id)
        return telemetry.available(run_id)
    except RunNotFound as exc:
        raise HTTPException(status_code=404, detail="run not found") from exc
    except KeyError as exc:
        raise HTTPException(status_code=404, detail="telemetry artifact not found") from exc
    except (McapReadError, UnsafeArtifactPath) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc


@router.get("/runs/{run_id}/artifacts")
def run_artifacts(
    run_id: int,
    queries: Annotated[RunQueryService, Depends(get_run_queries)],
):
    try:
        return queries.artifacts_for(run_id)
    except RunNotFound as exc:
        raise HTTPException(status_code=404, detail="run not found") from exc


@router.get("/runs/{run_id}/artifacts/{kind}")
def download_artifact(
    run_id: int,
    kind: str,
    queries: Annotated[RunQueryService, Depends(get_run_queries)],
) -> FileResponse:
    if not kind.replace("_", "").isalnum():
        raise HTTPException(status_code=404, detail="artifact not found")
    try:
        path = queries.artifact_file(run_id, kind)
    except (RunNotFound, KeyError, FileNotFoundError, UnsafeArtifactPath) as exc:
        raise HTTPException(status_code=404, detail="artifact not found") from exc
    return FileResponse(path, filename=path.name, media_type="application/octet-stream")
