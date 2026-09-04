from __future__ import annotations

from pathlib import Path

from app.domain.errors import NotFound
from app.domain.models import Artifact, Run, RunDetail, RunStatus, RunSummary
from app.repositories.instances import InstanceRepository
from app.repositories.runs import RunRepository
from app.services.artifacts import ArtifactService
from app.services.scenarios import ScenarioService


class RunNotFound(NotFound):
    pass


class RunQueryService:
    """Application queries for Run details, metrics, and owned artifacts."""

    def __init__(
        self,
        runs: RunRepository,
        instances: InstanceRepository,
        scenarios: ScenarioService,
        artifacts: ArtifactService,
    ) -> None:
        self.runs = runs
        self.instances = instances
        self.scenarios = scenarios
        self.artifacts = artifacts

    def require(self, run_id: int) -> Run:
        try:
            return self.runs.get(run_id)
        except KeyError as exc:
            raise RunNotFound(f"run not found: {run_id}") from exc

    def list(
        self,
        *,
        status: RunStatus | None,
        scenario: str | None,
        limit: int,
    ) -> list[RunSummary]:
        return self.runs.list(status=status, scenario=scenario, limit=limit)

    def detail(self, run_id: int) -> RunDetail:
        run = self.require(run_id)
        if run.scenario_type is None:
            scenario_type = self.scenarios.scenario_type_from_snapshot(
                self.artifacts.resolve(run.scenario_path)
            )
            if scenario_type is not None:
                run = run.model_copy(update={"scenario_type": scenario_type})
        return RunDetail(
            run=run,
            metrics=self.runs.get_metrics(run_id),
            artifacts=self.artifacts_for(run_id),
            instance=self.instances.get_for_run(run_id),
        )

    def metrics(self, run_id: int) -> dict[str, float | None]:
        self.require(run_id)
        return {metric.name: metric.value for metric in self.runs.get_metrics(run_id)}

    def artifacts_for(self, run_id: int) -> list[Artifact]:
        self.require(run_id)
        return [
            self.artifacts.public(row) for row in self.runs.get_artifact_rows(run_id)
        ]

    def artifact_file(self, run_id: int, kind: str) -> Path:
        self.require(run_id)
        row = self.runs.get_artifact_row(run_id, kind)
        return self.artifacts.require_file(row["path"])
