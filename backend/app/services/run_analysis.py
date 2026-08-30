from __future__ import annotations

from pathlib import Path

import yaml

from app.analysis.roll_hold_analyzer import (
    RollHoldAnalysisVariants,
    RollHoldAnalyzer,
)
from app.repositories.runs import RunRepository
from app.services.artifacts import ArtifactService


class AnalyzerNotApplicable(ValueError):
    pass


class RunAnalysisService:
    """Resolve immutable Run inputs and delegate deterministic artifact analysis."""

    def __init__(
        self,
        runs: RunRepository,
        artifacts: ArtifactService,
        roll_hold: RollHoldAnalyzer,
    ) -> None:
        self.runs = runs
        self.artifacts = artifacts
        self.roll_hold = roll_hold

    def analyze_roll_hold(self, run_id: int) -> RollHoldAnalysisVariants:
        run = self.runs.get(run_id)
        scenario_path = Path(run.scenario_path)
        definition = yaml.safe_load(scenario_path.read_text(encoding="utf-8"))
        scenario_type = definition.get("scenario_type") if isinstance(definition, dict) else None
        if scenario_type != "roll_hold":
            raise AnalyzerNotApplicable("Roll Hold analyzer requires a roll_hold scenario")
        telemetry_row = self.runs.get_artifact_row(run_id, "telemetry")
        telemetry_path = self.artifacts.resolve(telemetry_row["path"])
        decoded_variants = self.roll_hold.reader.variants(telemetry_path)
        variants = decoded_variants or run.variants or [run.execution_variant]
        return RollHoldAnalysisVariants(variants={
            variant: self.roll_hold.analyze(
                scenario_path, telemetry_path, variant=variant
            )
            for variant in variants
        })
