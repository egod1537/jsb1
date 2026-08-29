from __future__ import annotations

import asyncio
import shutil
from pathlib import Path
from typing import Annotated, Any

import numpy as np
from fastapi import APIRouter, Depends, HTTPException, Query, Request
from fastapi.responses import FileResponse

from app.analysis.downsampling import uniform_downsample
from app.analysis.mcap_reader import McapReadError, McapRunReader, canonical_name
from app.api.dependencies import (
    get_build_manager,
    get_build_scheduler,
    get_build_info,
    get_instances,
    get_app_settings,
    get_artifact_service,
    get_database,
    get_reader,
    get_repository,
    get_repository_manager,
    get_scenarios,
    get_scheduler,
)
from app.api.build_routes import router as build_router
from app.api.repository_routes import router as repository_router
from app.api.runtime_routes import router as runtime_router
from app.api.deployment_routes import router as deployment_router
from app.config.settings import Settings
from app.domain.models import RunCreate, RunDetail, RunStatus, SignalResponse
from app.domain.build_info import BuildInfo
from app.repositories.instances import InstanceRepository
from app.services.build_manager import BuildManager
from app.repositories.database import Database
from app.repositories.runs import RunRepository
from app.services.artifacts import ArtifactService, UnsafeArtifactPath
from app.services.scenarios import InvalidScenario, ScenarioService
from app.services.repository_manager import (
    GitOperationError,
    InvalidRepositoryPath,
    RepositoryManager,
    RuntimeRepositoryNotConfigured,
)
from app.workers.build_scheduler import InProcessBuildScheduler
from app.workers.scheduler import InProcessRunScheduler


router = APIRouter(prefix="/api")
router.include_router(repository_router)
router.include_router(runtime_router)
router.include_router(build_router)
router.include_router(deployment_router)
ANGULAR_SIGNALS = {
    "commanded_roll": "deg",
    "roll": "deg",
    "commanded_roll_rate": "deg/s",
    "roll_rate": "deg/s",
    "aileron": "deg",
}
RAD_TO_DEG = 180.0 / np.pi


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


@router.get("/scenarios")
def scenarios(service: Annotated[ScenarioService, Depends(get_scenarios)]) -> list[str]:
    return service.list()


@router.get("/autopilots")
def autopilots(
    settings: Annotated[Settings, Depends(get_app_settings)],
) -> list[str]:
    return settings.autopilots


@router.post("/runs", status_code=202)
async def create_run(
    body: RunCreate,
    repository: Annotated[RunRepository, Depends(get_repository)],
    scenarios: Annotated[ScenarioService, Depends(get_scenarios)],
    scheduler: Annotated[InProcessRunScheduler, Depends(get_scheduler)],
    settings: Annotated[Settings, Depends(get_app_settings)],
    build_manager: Annotated[BuildManager, Depends(get_build_manager)],
    build_scheduler: Annotated[InProcessBuildScheduler, Depends(get_build_scheduler)],
    repository_manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
    instances: Annotated[InstanceRepository, Depends(get_instances)],
) -> dict[str, Any]:
    try:
        scenario_path = scenarios.resolve(body.scenario)
    except InvalidScenario as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    if body.autopilot not in settings.autopilots:
        raise HTTPException(status_code=422, detail="unknown autopilot")
    repository_id: int | None = None
    branch: str | None = None
    commit_sha = body.commit_sha
    build_id = body.build_id
    build_reused = False
    build_status: str | None = None
    if body.branch is not None:
        configured_runtime = body.repository_id is None
        if configured_runtime:
            try:
                runtime_repository = await asyncio.to_thread(
                    repository_manager.runtime_repository,
                    settings.runtime_repository_name,
                )
            except RuntimeRepositoryNotConfigured as exc:
                raise HTTPException(status_code=503, detail=str(exc)) from exc
            except (GitOperationError, InvalidRepositoryPath) as exc:
                raise HTTPException(
                    status_code=503,
                    detail="JSB0 Runtime repository is not configured",
                ) from exc
            repository_id = runtime_repository.id
        else:
            repository_id = body.repository_id
        assert repository_id is not None
        try:
            await asyncio.to_thread(repository_manager.fetch, repository_id)
        except KeyError as exc:
            detail = (
                "JSB0 Runtime repository is not configured"
                if configured_runtime
                else "repository not found"
            )
            raise HTTPException(status_code=404, detail=detail) from exc
        except (GitOperationError, InvalidRepositoryPath) as exc:
            detail = (
                "Could not refresh JSB0 branches"
                if configured_runtime
                else str(exc)
            )
            raise HTTPException(
                status_code=502 if configured_runtime else 422, detail=detail
            ) from exc
        try:
            resolved = await asyncio.to_thread(
                repository_manager.resolve_branch, repository_id, body.branch
            )
        except GitOperationError as exc:
            if str(exc).startswith("branch not found:"):
                raise HTTPException(
                    status_code=404, detail="Branch no longer exists"
                ) from exc
            detail = "Could not resolve JSB0 branch" if configured_runtime else str(exc)
            raise HTTPException(status_code=422, detail=detail) from exc
        except (KeyError, InvalidRepositoryPath) as exc:
            detail = (
                "Could not resolve JSB0 branch" if configured_runtime else str(exc)
            )
            raise HTTPException(status_code=422, detail=detail) from exc
        try:
            build, build_reused = await asyncio.to_thread(
                build_manager.request_resolved, resolved
            )
        except (KeyError, InvalidRepositoryPath, RuntimeError) as exc:
            status_code = 404 if isinstance(exc, KeyError) else 422
            raise HTTPException(status_code=status_code, detail=str(exc)) from exc
        branch = body.branch
        commit_sha = resolved.commit_sha
        build_id = build.id
        build_status = build.status.value
    elif body.build_id is not None:
        try:
            build, _ = build_manager.require_runnable(body.build_id)
        except (KeyError, RuntimeError, InvalidRepositoryPath) as exc:
            status_code = 404 if isinstance(exc, KeyError) else 422
            raise HTTPException(status_code=status_code, detail=str(exc)) from exc
        if commit_sha is not None and commit_sha != build.commit_sha:
            raise HTTPException(
                status_code=422, detail="commit_sha does not match selected build"
            )
        repository_id = build.repository_id
        commit_sha = build.commit_sha
    run = repository.create(
        repository_id=repository_id,
        branch=branch,
        build_id=build_id,
        commit_sha=commit_sha,
        scenario_name=body.scenario,
        scenario_path=str(scenario_path),
        autopilot=body.autopilot,
    )
    relative_output = f"runs/{run.id:06d}"
    repository.set_output_directory(run.id, relative_output)
    if build_id is not None:
        instances.create(build_id=build_id, run_id=run.id)
    if branch is not None and not build_reused:
        assert build_id is not None
        build_scheduler.submit(build_id)
    scheduler.submit(
        run.id,
        wait_for_build_id=build_id if branch is not None else None,
    )
    return {
        "id": run.id,
        "status": RunStatus.QUEUED.value,
        "repository_id": repository_id,
        "branch": branch,
        "build_id": build_id,
        "build_status": build_status,
        "build_reused": build_reused,
        "commit_sha": commit_sha,
    }


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
) -> RunDetail:
    run = require_run(repository, run_id)
    return RunDetail(
        run=run,
        metrics=repository.get_metrics(run_id),
        artifacts=[artifacts.public(row) for row in repository.get_artifact_rows(run_id)],
        instance=instances.get_for_run(run_id),
    )


@router.get("/runs/{run_id}/metrics")
def run_metrics(
    run_id: int,
    repository: Annotated[RunRepository, Depends(get_repository)],
) -> dict[str, float | None]:
    require_run(repository, run_id)
    return {metric.name: metric.value for metric in repository.get_metrics(run_id)}


@router.get("/runs/{run_id}/signals", response_model=SignalResponse)
def run_signals(
    run_id: int,
    repository: Annotated[RunRepository, Depends(get_repository)],
    reader: Annotated[McapRunReader, Depends(get_reader)],
    artifacts: Annotated[ArtifactService, Depends(get_artifact_service)],
    channels: Annotated[str, Query(min_length=1)],
    start: Annotated[float | None, Query(ge=0)] = None,
    end: Annotated[float | None, Query(ge=0)] = None,
    max_points: Annotated[int, Query(ge=10, le=20_000)] = 2000,
) -> SignalResponse:
    require_run(repository, run_id)
    if start is not None and end is not None and end < start:
        raise HTTPException(status_code=422, detail="end must be greater than or equal to start")
    names = [canonical_name(item) for item in channels.split(",") if item.strip()]
    if not names or len(names) > 20:
        raise HTTPException(status_code=422, detail="request between 1 and 20 channels")
    try:
        row = repository.get_artifact_row(run_id, "telemetry")
        telemetry_path = artifacts.resolve(row["path"])
        time, values = reader.read_aligned(telemetry_path, names, start=start, end=end)
    except KeyError as exc:
        raise HTTPException(status_code=404, detail="telemetry artifact not found") from exc
    except (McapReadError, UnsafeArtifactPath) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    source_points = len(time)
    time, values = uniform_downsample(time, values, max_points)
    units: dict[str, str] = {}
    for name in list(values):
        if name in ANGULAR_SIGNALS:
            values[name] = values[name] * RAD_TO_DEG
            units[name] = ANGULAR_SIGNALS[name]
        else:
            units[name] = "raw"
    return SignalResponse(
        time=time.tolist(),
        series={name: value.tolist() for name, value in values.items()},
        units=units,
        source_points=source_points,
        returned_points=len(time),
    )


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
