from __future__ import annotations

import yaml
from jsb1_analysis.analyzers.registry import (
    AnalyzerMissingSignalError,
    AnalyzerRegistry,
    UnsupportedAnalyzerError,
)
from jsb1_analysis.analyzers import CourseHoldAnalyzer, PitchHoldAnalyzer, TecsAnalyzer

from app.analysis.roll_hold_analyzer import (
    RollHoldAnalysisVariants,
    RollHoldAnalyzer,
)
from app.repositories.runs import RunRepository
from app.services.artifacts import ArtifactService
from app.services.repository_manager import RepositoryManager
from app.services.runtime_contract import RuntimeContractReader


class AnalyzerNotApplicable(UnsupportedAnalyzerError, ValueError):
    pass


class RunAnalysisService:
    """Resolve immutable Run inputs and delegate deterministic artifact analysis."""

    def __init__(
        self,
        runs: RunRepository,
        artifacts: ArtifactService,
        roll_hold: RollHoldAnalyzer,
        repositories: RepositoryManager | None = None,
        contract_reader: RuntimeContractReader | None = None,
        course_hold: CourseHoldAnalyzer | None = None,
        pitch_hold: PitchHoldAnalyzer | None = None,
        tecs: TecsAnalyzer | None = None,
    ) -> None:
        self.runs = runs
        self.artifacts = artifacts
        self.roll_hold = roll_hold
        self.course_hold = course_hold or CourseHoldAnalyzer()
        self.pitch_hold = pitch_hold or PitchHoldAnalyzer()
        self.tecs = tecs or TecsAnalyzer()
        self.registry = AnalyzerRegistry(
            (roll_hold, self.course_hold, self.pitch_hold, self.tecs)
        )
        self.repositories = repositories
        self.contract_reader = contract_reader or RuntimeContractReader()

    def analyze_roll_hold(self, run_id: int) -> RollHoldAnalysisVariants:
        run = self.runs.get(run_id)
        scenario_path = self.artifacts.resolve(run.scenario_path)
        definition = yaml.safe_load(scenario_path.read_text(encoding="utf-8"))
        scenario_type = (
            definition.get("scenario_type") if isinstance(definition, dict) else None
        )
        if scenario_type != "roll_hold":
            raise AnalyzerNotApplicable(
                f"no analyzer supports scenario type {scenario_type!r}"
            )
        telemetry_row = self.runs.get_artifact_row(run_id, "telemetry")
        telemetry_path = self.artifacts.resolve(telemetry_row["path"])
        signal_catalog = None
        descriptor = None
        contract_variants: tuple[str, ...] = ()
        if (
            self.repositories is not None
            and run.repository_id is not None
            and run.commit_sha is not None
            and run.branch is not None
        ):
            worktree = self.repositories.prepare_worktree(
                run.repository_id, run.commit_sha
            )
            if self.contract_reader.is_indexed(worktree):
                bundle = self.contract_reader.load_bundle(
                    worktree,
                    repository_id=run.repository_id,
                    commit_sha=run.commit_sha,
                )
                signal_catalog = bundle.signal_catalog
                descriptor = bundle.telemetry_descriptor
                contract_variants = bundle.variants
        dataset = self.roll_hold.reader.dataset(
            telemetry_path,
            signal_catalog=signal_catalog,
            descriptor=descriptor,
            variants=contract_variants,
        )
        variants = list(dataset.variants()) or run.variants or [run.execution_variant]
        return RollHoldAnalysisVariants(
            variants={
                variant: self.registry.analyze(
                    str(scenario_type),
                    dataset,
                    variant=variant,
                    scenario_definition=definition,
                )
                for variant in variants
            }
        )

    def analyze_course_hold(self, run_id: int) -> dict[str, object]:
        run = self.runs.get(run_id)
        scenario_path = self.artifacts.resolve(run.scenario_path)
        definition = yaml.safe_load(scenario_path.read_text(encoding="utf-8"))
        scenario_type = (
            definition.get("scenario_type") if isinstance(definition, dict) else None
        )
        if scenario_type != "course_hold":
            raise AnalyzerNotApplicable(
                f"no Course Hold analyzer supports scenario type {scenario_type!r}"
            )
        telemetry_row = self.runs.get_artifact_row(run_id, "telemetry")
        telemetry_path = self.artifacts.resolve(telemetry_row["path"])
        signal_catalog = None
        descriptor = None
        contract_variants: tuple[str, ...] = ()
        if (
            self.repositories is not None
            and run.repository_id is not None
            and run.commit_sha is not None
            and run.branch is not None
        ):
            worktree = self.repositories.prepare_worktree(
                run.repository_id, run.commit_sha
            )
            if self.contract_reader.is_indexed(worktree):
                bundle = self.contract_reader.load_bundle(
                    worktree,
                    repository_id=run.repository_id,
                    commit_sha=run.commit_sha,
                )
                signal_catalog = bundle.signal_catalog
                descriptor = bundle.telemetry_descriptor
                contract_variants = bundle.variants
        dataset = self.roll_hold.reader.dataset(
            telemetry_path,
            signal_catalog=signal_catalog,
            descriptor=descriptor,
            variants=contract_variants,
        )
        variants = list(dataset.variants()) or run.variants or [run.execution_variant]
        return {
            "analyzer": "course_hold",
            "variants": {
                variant: self.registry.analyze(
                    "course_hold",
                    dataset,
                    variant=variant,
                    scenario_definition=definition,
                ).as_dict()
                for variant in variants
            },
        }

    def analyze_pitch_hold(self, run_id: int) -> dict[str, object]:
        run = self.runs.get(run_id)
        scenario_path = self.artifacts.resolve(run.scenario_path)
        definition = yaml.safe_load(scenario_path.read_text(encoding="utf-8"))
        scenario_type = (
            definition.get("scenario_type") if isinstance(definition, dict) else None
        )
        if scenario_type != "pitch_hold":
            raise AnalyzerNotApplicable(
                f"no Pitch Hold analyzer supports scenario type {scenario_type!r}"
            )
        telemetry_row = self.runs.get_artifact_row(run_id, "telemetry")
        telemetry_path = self.artifacts.resolve(telemetry_row["path"])
        signal_catalog = None
        descriptor = None
        contract_variants: tuple[str, ...] = ()
        if (
            self.repositories is not None
            and run.repository_id is not None
            and run.commit_sha is not None
            and run.branch is not None
        ):
            worktree = self.repositories.prepare_worktree(
                run.repository_id, run.commit_sha
            )
            if self.contract_reader.is_indexed(worktree):
                bundle = self.contract_reader.load_bundle(
                    worktree,
                    repository_id=run.repository_id,
                    commit_sha=run.commit_sha,
                )
                signal_catalog = bundle.signal_catalog
                descriptor = bundle.telemetry_descriptor
                contract_variants = bundle.variants
        dataset = self.roll_hold.reader.dataset(
            telemetry_path,
            signal_catalog=signal_catalog,
            descriptor=descriptor,
            variants=contract_variants,
        )
        variants = list(dataset.variants()) or run.variants or [run.execution_variant]
        return {
            "analyzer": "pitch_hold",
            "variants": {
                variant: self.registry.analyze(
                    "pitch_hold",
                    dataset,
                    variant=variant,
                    scenario_definition=definition,
                ).as_dict()
                for variant in variants
            },
        }

    def analyze_tecs(self, run_id: int) -> dict[str, object]:
        run = self.runs.get(run_id)
        scenario_path = self.artifacts.resolve(run.scenario_path)
        definition = yaml.safe_load(scenario_path.read_text(encoding="utf-8"))
        scenario_type = (
            definition.get("scenario_type") if isinstance(definition, dict) else None
        )
        if scenario_type != "tecs":
            raise AnalyzerNotApplicable(
                f"no TECS analyzer supports scenario type {scenario_type!r}"
            )
        telemetry_row = self.runs.get_artifact_row(run_id, "telemetry")
        telemetry_path = self.artifacts.resolve(telemetry_row["path"])
        signal_catalog = None
        descriptor = None
        contract_variants: tuple[str, ...] = ()
        minimum_safe_airspeed_mps = None
        maximum_safe_airspeed_mps = None
        if (
            self.repositories is not None
            and run.repository_id is not None
            and run.commit_sha is not None
            and run.branch is not None
        ):
            worktree = self.repositories.prepare_worktree(
                run.repository_id, run.commit_sha
            )
            if self.contract_reader.is_indexed(worktree):
                bundle = self.contract_reader.load_bundle(
                    worktree,
                    repository_id=run.repository_id,
                    commit_sha=run.commit_sha,
                )
                signal_catalog = bundle.signal_catalog
                descriptor = bundle.telemetry_descriptor
                contract_variants = bundle.variants
                definitions = {item.id: item for item in bundle.parameters}
                if "FW_AIRSPD_MIN" in definitions:
                    minimum_safe_airspeed_mps = run.controller_parameters.get(
                        "FW_AIRSPD_MIN", definitions["FW_AIRSPD_MIN"].default_value
                    )
                if "FW_AIRSPD_MAX" in definitions:
                    maximum_safe_airspeed_mps = run.controller_parameters.get(
                        "FW_AIRSPD_MAX", definitions["FW_AIRSPD_MAX"].default_value
                    )
        dataset = self.roll_hold.reader.dataset(
            telemetry_path,
            signal_catalog=signal_catalog,
            descriptor=descriptor,
            variants=contract_variants,
        )
        variants = list(dataset.variants()) or run.variants or [run.execution_variant]
        return {
            "analyzer": "tecs",
            "variants": {
                variant: self.registry.analyze(
                    "tecs",
                    dataset,
                    variant=variant,
                    scenario_definition=definition,
                    minimum_safe_airspeed_mps=minimum_safe_airspeed_mps,
                    maximum_safe_airspeed_mps=maximum_safe_airspeed_mps,
                ).as_dict()
                for variant in variants
            },
        }


__all__ = [
    "AnalyzerMissingSignalError",
    "AnalyzerNotApplicable",
    "RunAnalysisService",
]
