from __future__ import annotations

from dataclasses import dataclass

from app.domain.errors import NotFound
from app.repositories.comparisons import ComparisonRepository
from app.repositories.runs import RunRepository
from app.services.artifacts import ArtifactService


class ScenarioSnapshotNotFound(NotFound):
    pass


class ComparisonSnapshotOwnerNotFound(ScenarioSnapshotNotFound):
    pass


@dataclass(frozen=True)
class ScenarioSnapshot:
    id: int
    name: str
    relative_path: str
    yaml_text: str
    sha256: str | None
    repository_id: int | None
    branch: str | None
    commit_sha: str | None


class ScenarioSnapshotQueryService:
    """Loads frozen scenario content without exposing persistence or paths to routes."""

    def __init__(
        self,
        runs: RunRepository,
        comparisons: ComparisonRepository,
        artifacts: ArtifactService,
    ) -> None:
        self.runs = runs
        self.comparisons = comparisons
        self.artifacts = artifacts

    def for_run(self, run_id: int) -> ScenarioSnapshot:
        try:
            run = self.runs.get(run_id)
            artifact = self.runs.get_artifact_row(run_id, "scenario")
            yaml_text = self.artifacts.read_text(artifact["path"])
        except (KeyError, OSError, UnicodeError, ValueError) as exc:
            raise ScenarioSnapshotNotFound(
                f"run scenario snapshot not found: {run_id}"
            ) from exc
        return ScenarioSnapshot(
            id=run.id,
            name=run.scenario_name,
            relative_path=artifact["path"],
            yaml_text=yaml_text,
            sha256=run.scenario_sha256,
            repository_id=run.repository_id,
            branch=run.branch,
            commit_sha=run.commit_sha,
        )

    def for_comparison(self, comparison_id: int) -> ScenarioSnapshot:
        try:
            comparison = self.comparisons.get(comparison_id)
        except KeyError as exc:
            raise ComparisonSnapshotOwnerNotFound(
                f"comparison not found: {comparison_id}"
            ) from exc
        try:
            yaml_text, relative_path = self.artifacts.read_managed_text(
                comparison.scenario_path
            )
        except (OSError, UnicodeError, ValueError) as exc:
            raise ScenarioSnapshotNotFound(
                f"comparison scenario snapshot not found: {comparison_id}"
            ) from exc
        return ScenarioSnapshot(
            id=comparison.id,
            name=comparison.scenario_name,
            relative_path=relative_path,
            yaml_text=yaml_text,
            sha256=comparison.scenario_sha256,
            repository_id=comparison.repository_id,
            branch=comparison.branch,
            commit_sha=comparison.commit_sha,
        )
