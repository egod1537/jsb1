from __future__ import annotations

import asyncio
from pathlib import Path
from typing import Annotated, Literal

from fastapi import APIRouter, Depends, HTTPException, Query

from app.api.dependencies import (
    get_app_settings,
    get_artifact_service,
    get_comparisons,
    get_repository,
    get_repository_manager,
    get_scenario_compatibility,
    get_scenario_inspector,
    get_scenario_validator,
)
from app.config.settings import Settings
from app.domain.scenario import ScenarioInspectionCatalogEntry, ScenarioInspectionDetail
from app.repositories.comparisons import ComparisonRepository
from app.repositories.runs import RunRepository
from app.services.artifacts import ArtifactService, UnsafeArtifactPath
from app.services.repository_manager import RepositoryManager
from app.services.scenario_inspection import ScenarioInspectionService
from app.services.scenario_sync import ScenarioCompatibilityResolver
from app.services.scenario_validator import ScenarioValidator
from app.services.scenarios import InvalidScenario


router = APIRouter(tags=["scenario inspection"])


async def _compatibility_contract(
    resolver: ScenarioCompatibilityResolver,
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
            detail="JSB0 main scenario contract is unavailable",
        ) from exc
    return compatibility, contract


async def _historical_contract(
    manager: RepositoryManager,
    validator: ScenarioValidator,
    repository_id: int | None,
    commit_sha: str | None,
):
    if repository_id is None or not commit_sha:
        return None
    try:
        worktree = await asyncio.to_thread(
            manager.prepare_worktree, repository_id, commit_sha
        )
        return await asyncio.to_thread(validator.load_runtime_contract, worktree)
    except Exception:
        # Historical content remains inspectable even when its old worktree or
        # contract is no longer locally available.
        return None


@router.get(
    "/scenario-catalog",
    response_model=list[ScenarioInspectionCatalogEntry],
)
async def scenario_catalog(
    inspector: Annotated[ScenarioInspectionService, Depends(get_scenario_inspector)],
    resolver: Annotated[ScenarioCompatibilityResolver, Depends(get_scenario_compatibility)],
    validator: Annotated[ScenarioValidator, Depends(get_scenario_validator)],
):
    compatibility, contract = await _compatibility_contract(resolver, validator)
    return await asyncio.to_thread(
        inspector.catalog, contract, compatibility.runtime
    )


@router.get("/scenario-catalog/detail", response_model=ScenarioInspectionDetail)
async def scenario_detail(
    inspector: Annotated[ScenarioInspectionService, Depends(get_scenario_inspector)],
    resolver: Annotated[ScenarioCompatibilityResolver, Depends(get_scenario_compatibility)],
    validator: Annotated[ScenarioValidator, Depends(get_scenario_validator)],
    source: Annotated[Literal["bundled", "managed", "sftp"], Query()],
    scenario_id: Annotated[str, Query(alias="id", min_length=1, max_length=1024)],
):
    compatibility, contract = await _compatibility_contract(resolver, validator)
    try:
        return await asyncio.to_thread(
            inspector.detail,
            source,
            scenario_id,
            contract,
            compatibility.runtime,
        )
    except InvalidScenario as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc


@router.get("/runs/{run_id}/scenario", response_model=ScenarioInspectionDetail)
async def run_scenario_snapshot(
    run_id: int,
    runs: Annotated[RunRepository, Depends(get_repository)],
    artifacts: Annotated[ArtifactService, Depends(get_artifact_service)],
    inspector: Annotated[ScenarioInspectionService, Depends(get_scenario_inspector)],
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
    validator: Annotated[ScenarioValidator, Depends(get_scenario_validator)],
):
    try:
        run = runs.get(run_id)
        artifact = runs.get_artifact_row(run_id, "scenario")
        snapshot = artifacts.resolve(artifact["path"])
        yaml_text = snapshot.read_text(encoding="utf-8")
    except KeyError as exc:
        raise HTTPException(status_code=404, detail="run scenario snapshot not found") from exc
    except (UnsafeArtifactPath, OSError, UnicodeError) as exc:
        raise HTTPException(status_code=404, detail="run scenario snapshot not found") from exc
    contract = await _historical_contract(
        manager, validator, run.repository_id, run.commit_sha
    )
    return inspector.snapshot_detail(
        scenario_id=str(run.id),
        name=run.scenario_name,
        relative_path=artifact["path"],
        yaml_text=yaml_text,
        expected_sha256=run.scenario_sha256,
        authority="frozen run snapshot",
        contract=contract,
        runtime_branch=run.branch,
        runtime_commit=run.commit_sha,
    )


@router.get(
    "/comparisons/{comparison_id}/scenario",
    response_model=ScenarioInspectionDetail,
)
async def comparison_scenario_snapshot(
    comparison_id: int,
    comparisons: Annotated[ComparisonRepository, Depends(get_comparisons)],
    settings: Annotated[Settings, Depends(get_app_settings)],
    inspector: Annotated[ScenarioInspectionService, Depends(get_scenario_inspector)],
    manager: Annotated[RepositoryManager, Depends(get_repository_manager)],
    validator: Annotated[ScenarioValidator, Depends(get_scenario_validator)],
):
    try:
        comparison = comparisons.get(comparison_id)
    except KeyError as exc:
        raise HTTPException(status_code=404, detail="comparison not found") from exc
    snapshot = Path(comparison.scenario_path).resolve()
    data_root = settings.data_dir.resolve()
    if not snapshot.is_relative_to(data_root):
        raise HTTPException(status_code=404, detail="comparison scenario snapshot not found")
    try:
        yaml_text = snapshot.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise HTTPException(
            status_code=404, detail="comparison scenario snapshot not found"
        ) from exc
    contract = await _historical_contract(
        manager, validator, comparison.repository_id, comparison.commit_sha
    )
    return inspector.snapshot_detail(
        scenario_id=f"comparison-{comparison.id}",
        name=comparison.scenario_name,
        relative_path=str(snapshot.relative_to(data_root)),
        yaml_text=yaml_text,
        expected_sha256=comparison.scenario_sha256,
        authority="frozen comparison snapshot",
        contract=contract,
        runtime_branch=comparison.branch,
        runtime_commit=comparison.commit_sha,
    )
