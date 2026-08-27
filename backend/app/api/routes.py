from __future__ import annotations

import shutil
from pathlib import Path
from typing import Annotated, Any

import numpy as np
from fastapi import APIRouter, Depends, HTTPException, Query, Request
from fastapi.responses import FileResponse

from app.analysis.downsampling import uniform_downsample
from app.analysis.mcap_reader import McapReadError, McapRunReader, canonical_name
from app.api.dependencies import (
    get_app_settings,
    get_artifact_service,
    get_database,
    get_reader,
    get_repository,
    get_scenarios,
    get_scheduler,
)
from app.config.settings import Settings
from app.domain.models import RunCreate, RunDetail, RunStatus, SignalResponse
from app.repositories.database import Database
from app.repositories.runs import RunRepository
from app.services.artifacts import ArtifactService, UnsafeArtifactPath
from app.services.scenarios import InvalidScenario, ScenarioService
from app.workers.scheduler import InProcessRunScheduler


router = APIRouter(prefix="/api")
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


@router.get("/scenarios")
def scenarios(service: Annotated[ScenarioService, Depends(get_scenarios)]) -> list[str]:
    return service.list()


@router.get("/autopilots")
def autopilots(
    settings: Annotated[Settings, Depends(get_app_settings)],
) -> list[str]:
    return settings.autopilots


@router.post("/runs", status_code=202)
def create_run(
    body: RunCreate,
    repository: Annotated[RunRepository, Depends(get_repository)],
    scenarios: Annotated[ScenarioService, Depends(get_scenarios)],
    scheduler: Annotated[InProcessRunScheduler, Depends(get_scheduler)],
    settings: Annotated[Settings, Depends(get_app_settings)],
) -> dict[str, Any]:
    try:
        scenario_path = scenarios.resolve(body.scenario)
    except InvalidScenario as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    if body.autopilot not in settings.autopilots:
        raise HTTPException(status_code=422, detail="unknown autopilot")
    run = repository.create(
        commit_sha=body.commit_sha,
        scenario_name=body.scenario,
        scenario_path=str(scenario_path),
        autopilot=body.autopilot,
    )
    relative_output = f"runs/{run.id:06d}"
    repository.set_output_directory(run.id, relative_output)
    scheduler.submit(run.id)
    return {"id": run.id, "status": RunStatus.QUEUED.value}


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
) -> RunDetail:
    run = require_run(repository, run_id)
    return RunDetail(
        run=run,
        metrics=repository.get_metrics(run_id),
        artifacts=[artifacts.public(row) for row in repository.get_artifact_rows(run_id)],
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
