from __future__ import annotations

import asyncio
import shutil
from pathlib import Path
from typing import Annotated, Any

from fastapi import APIRouter, Depends, HTTPException, Query, Response
from fastapi.responses import FileResponse

from app.analysis.mcap_reader import McapReadError
from app.analysis.roll_hold_analyzer import RollHoldAnalysisVariants
from app.api.build_routes import router as build_router
from app.api.comparison_routes import router as comparison_router
from app.api.dependencies import (
    get_app_settings,
    get_artifact_service,
    get_build_info,
    get_run_creation,
    get_run_deletion,
    get_run_analysis,
    get_runtime_variants,
    get_database,
    get_instances,
    get_telemetry_queries,
    get_repository,
    get_scenarios,
)
from app.api.deployment_routes import router as deployment_router
from app.api.repository_routes import router as repository_router
from app.api.runtime_routes import router as runtime_router
from app.api.scenario_routes import router as scenario_router
from app.api.scenario_inspection_routes import router as scenario_inspection_router
from app.config.settings import Settings
from app.domain.build_info import BuildInfo
from app.domain.models import (
    AvailableSignalsResponse,
    RunCreate,
    RunDetail,
    RunStatus,
    SignalResponse,
)
from app.repositories.database import Database
from app.repositories.instances import InstanceRepository
from app.repositories.runs import RunRepository
from app.services.artifacts import ArtifactService, UnsafeArtifactPath
from app.domain.errors import SnapshotWriteFailed
from app.services.repository_manager import (
    GitOperationError,
    InvalidRepositoryPath,
    RuntimeRepositoryNotConfigured,
    RuntimeRepositoryUnavailable,
)
from app.services.scenarios import InvalidScenario, ScenarioService
from app.services.runtime_variants import RuntimeVariantContractError
from app.services.run_creation import CreateRunCommand, RunCreationService
from app.services.run_analysis import AnalyzerNotApplicable, RunAnalysisService
from app.services.run_deletion import (
    ActiveRunDeletionNotAllowed,
    RunDeletionService,
    UnsafeRunDirectory,
)
from app.services.telemetry_queries import TelemetryQueryService

router = APIRouter(prefix="/api")
router.include_router(repository_router)
router.include_router(runtime_router)
router.include_router(build_router)
router.include_router(deployment_router)
router.include_router(scenario_router)
router.include_router(scenario_inspection_router)
router.include_router(comparison_router)


def require_run(repository: RunRepository, run_id: int):
    try:
        return repository.get(run_id)
    except KeyError as exc:
        raise HTTPException(status_code=404, detail="run not found") from exc


@router.get("/health")
def health(
    database: Annotated[Database, Depends(get_database)],
    settings: Annotated[Settings, Depends(get_app_settings)],
) -> dict[str, Any]:
    executable = str(settings.runner_path)
    runner_available = settings.runner_path.is_file() or shutil.which(executable) is not None
    database_ok = database.ping()
    return {
        "status": "ok" if database_ok else "degraded",
        "runner_available": runner_available,
        "database": "ok" if database_ok else "error",
    }


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
    except GitOperationError as exc:
        if str(exc).startswith("branch not found:"):
            raise HTTPException(status_code=404, detail="Branch no longer exists") from exc
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    except KeyError as exc:
        raise HTTPException(status_code=404, detail="repository or build not found") from exc
    except (InvalidScenario, RuntimeVariantContractError, InvalidRepositoryPath, ValueError, RuntimeError, OSError) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc


@router.get("/runs")
def list_runs(
    repository: Annotated[RunRepository, Depends(get_repository)],
    status: RunStatus | None = None,
    scenario: str | None = None,
    limit: Annotated[int, Query(ge=1, le=200)] = 50,
):
    return repository.list(status=status, scenario=scenario, limit=limit)


@router.get("/runs/{run_id}", response_model=RunDetail)
def run_detail(
    run_id: int,
    repository: Annotated[RunRepository, Depends(get_repository)],
    artifacts: Annotated[ArtifactService, Depends(get_artifact_service)],
    instances: Annotated[InstanceRepository, Depends(get_instances)],
    scenarios: Annotated[ScenarioService, Depends(get_scenarios)],
) -> RunDetail:
    run = require_run(repository, run_id)
    if run.scenario_type is None:
        scenario_type = scenarios.scenario_type_from_snapshot(Path(run.scenario_path))
        if scenario_type is not None:
            run = run.model_copy(update={"scenario_type": scenario_type})
    return RunDetail(
        run=run,
        metrics=repository.get_metrics(run_id),
        artifacts=[artifacts.public(row) for row in repository.get_artifact_rows(run_id)],
        instance=instances.get_for_run(run_id),
    )


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
    repository: Annotated[RunRepository, Depends(get_repository)],
) -> dict[str, float | None]:
    require_run(repository, run_id)
    return {metric.name: metric.value for metric in repository.get_metrics(run_id)}


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
    repository: Annotated[RunRepository, Depends(get_repository)],
    telemetry: Annotated[TelemetryQueryService, Depends(get_telemetry_queries)],
    signals: Annotated[str | None, Query(min_length=1)] = None,
    channels: Annotated[str | None, Query(min_length=1, deprecated=True)] = None,
    variant: Annotated[str | None, Query(min_length=1, max_length=64)] = None,
    start: Annotated[float | None, Query(ge=0)] = None,
    end: Annotated[float | None, Query(ge=0)] = None,
    max_points: Annotated[int, Query(ge=10, le=20_000)] = 2000,
) -> SignalResponse:
    require_run(repository, run_id)
    try:
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
    except KeyError as exc:
        raise HTTPException(status_code=404, detail="telemetry artifact not found") from exc
    except (McapReadError, UnsafeArtifactPath, ValueError) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc


@router.get(
    "/runs/{run_id}/signals/available",
    response_model=AvailableSignalsResponse,
)
def available_run_signals(
    run_id: int,
    repository: Annotated[RunRepository, Depends(get_repository)],
    telemetry: Annotated[TelemetryQueryService, Depends(get_telemetry_queries)],
) -> AvailableSignalsResponse:
    require_run(repository, run_id)
    try:
        return telemetry.available(run_id)
    except KeyError as exc:
        raise HTTPException(status_code=404, detail="telemetry artifact not found") from exc
    except (McapReadError, UnsafeArtifactPath) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc


@router.get("/runs/{run_id}/artifacts")
def run_artifacts(
    run_id: int,
    repository: Annotated[RunRepository, Depends(get_repository)],
    artifacts: Annotated[ArtifactService, Depends(get_artifact_service)],
):
    require_run(repository, run_id)
    return [artifacts.public(row) for row in repository.get_artifact_rows(run_id)]


@router.get("/runs/{run_id}/artifacts/{kind}")
def download_artifact(
    run_id: int,
    kind: str,
    repository: Annotated[RunRepository, Depends(get_repository)],
    artifacts: Annotated[ArtifactService, Depends(get_artifact_service)],
) -> FileResponse:
    require_run(repository, run_id)
    if not kind.replace("_", "").isalnum():
        raise HTTPException(status_code=404, detail="artifact not found")
    try:
        row = repository.get_artifact_row(run_id, kind)
        path = artifacts.resolve(row["path"])
    except (KeyError, UnsafeArtifactPath) as exc:
        raise HTTPException(status_code=404, detail="artifact not found") from exc
    if not path.is_file():
        raise HTTPException(status_code=404, detail="artifact file not found")
    return FileResponse(path, filename=path.name, media_type="application/octet-stream")
