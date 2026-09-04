from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, ClassVar

from app.domain.artifacts import StoredArtifact
from app.domain.errors import RuntimeArtifactError
from app.domain.models import Run
from app.infrastructure.filesystem import RunArtifactStore
from app.repositories.runs import RunRepository
from app.services.execution_plan import RunExecutionPlan


@dataclass(frozen=True)
class RuntimeManifestOutcome:
    status: str
    error: str | None
    simulation_time_sec: float | None
    telemetry_path: Path | None

    @property
    def succeeded(self) -> bool:
        return self.status == "completed"


class RunArtifactIngestionService:
    """Validate and register JSB0 outputs using the exact revision manifest."""

    KIND_BY_CONTRACT_TYPE: ClassVar[dict[str, str]] = {
        "run_metadata": "run",
        "scenario_snapshot": "scenario",
        "parameter_set_snapshot": "parameters",
        "telemetry": "telemetry",
    }

    def __init__(self, runs: RunRepository, files: RunArtifactStore) -> None:
        self.runs = runs
        self.files = files

    def ingest(
        self,
        run: Run,
        plan: RunExecutionPlan,
        *,
        process_exit_code: int,
    ) -> RuntimeManifestOutcome:
        manifest_path = self._artifact_path(plan, "run_metadata")
        if not self.files.is_file(manifest_path):
            raise RuntimeArtifactError(
                f"runner completed without {plan.artifact_manifest.path_for('run_metadata')}"
            )
        payload = self._read_manifest(manifest_path)
        self._validate_schema(payload, plan)
        self._validate_provenance(payload, run, plan)
        _, _, results = self._execution_metadata(
            payload, run, strict=plan.run_schema is not None
        )
        status = self._status(payload, results, process_exit_code)
        error = self._structured_error(payload, results, status)
        telemetry_path = self._artifact_path(plan, "telemetry")
        if status == "completed" and not self.files.is_file(telemetry_path):
            raise RuntimeArtifactError(
                f"runner completed without {plan.artifact_manifest.path_for('telemetry')}"
            )
        if not self.files.is_file(telemetry_path):
            telemetry_path = None

        published = self._published_artifacts(run, plan, status)
        self.runs.record_runtime_ingestion(
            run.id,
            results=results,
            artifacts=published,
        )
        return RuntimeManifestOutcome(
            status=status,
            error=error,
            simulation_time_sec=self._optional_number(payload.get("simulation_time_s")),
            telemetry_path=telemetry_path,
        )

    def _published_artifacts(
        self, run: Run, plan: RunExecutionPlan, status: str
    ) -> list[StoredArtifact]:
        published: list[StoredArtifact] = []
        for definition in plan.artifact_manifest.artifacts:
            path = self.files.path(plan.output_directory, definition.path)
            exists = self.files.is_file(path)
            required = definition.required and not (
                definition.type == "telemetry" and status != "completed"
            )
            if required and not exists:
                raise RuntimeArtifactError(
                    f"runner completed without {definition.path}"
                )
            if exists:
                metadata = self.files.metadata(
                    self.KIND_BY_CONTRACT_TYPE.get(definition.type, definition.type),
                    path,
                )
                expected_digest = (
                    run.scenario_sha256
                    if definition.type == "scenario_snapshot"
                    else run.parameter_snapshot_sha256
                    if definition.type == "parameter_set_snapshot"
                    else None
                )
                if expected_digest and metadata.sha256 != expected_digest:
                    raise RuntimeArtifactError(
                        f"runner modified immutable {definition.type}"
                    )
                published.append(metadata)
        return published

    def _read_manifest(self, path: Path) -> dict[str, Any]:
        try:
            payload = json.loads(self.files.read_text(path))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            raise RuntimeArtifactError("runner produced an invalid run.json") from exc
        if not isinstance(payload, dict):
            raise RuntimeArtifactError("runner run.json must be an object")
        return payload

    @staticmethod
    def _validate_schema(payload: dict[str, Any], plan: RunExecutionPlan) -> None:
        if plan.run_schema is None:
            return
        errors = sorted(
            plan.run_schema.iter_errors(payload),
            key=lambda error: tuple(str(item) for item in error.absolute_path),
        )
        if errors:
            error = errors[0]
            location = ".".join(str(item) for item in error.absolute_path)
            prefix = f"{location}: " if location else ""
            raise RuntimeArtifactError(
                f"runner run.json violates JSB0 contract: {prefix}{error.message}"
            )

    @staticmethod
    def _validate_provenance(
        payload: dict[str, Any], run: Run, plan: RunExecutionPlan
    ) -> None:
        if plan.run_schema is None:
            return
        runtime = payload.get("runtime")
        reported_commit = runtime.get("commit") if isinstance(runtime, dict) else None
        if reported_commit != plan.commit_sha:
            raise RuntimeArtifactError(
                "runner run.json Runtime commit does not match the selected immutable revision"
            )
        scenario = payload.get("scenario")
        reported_digest = (
            scenario.get("digest_sha256") if isinstance(scenario, dict) else None
        )
        if reported_digest and reported_digest != run.scenario_sha256:
            raise RuntimeArtifactError(
                "runner run.json Scenario digest does not match the frozen snapshot"
            )

    @staticmethod
    def _execution_metadata(
        payload: dict[str, Any], run: Run, *, strict: bool
    ) -> tuple[str, list[str], dict[str, dict[str, object]]]:
        execution = payload.get("execution")
        execution = execution if isinstance(execution, dict) else {}
        raw_variants = execution.get("variants", payload.get("variants"))
        variants = (
            [
                item
                for item in raw_variants or []
                if isinstance(item, str) and item.strip()
            ]
            if isinstance(raw_variants, list)
            else []
        )
        mode = payload.get("mode", execution.get("mode", "single"))
        if not isinstance(mode, str):
            mode = "single"
        mode = mode.strip().lower().replace("_", "-")
        if mode in {"compare-only", "comparison"}:
            mode = "compare"
        raw_results = payload.get("results", {})
        results = (
            {
                variant: value
                for variant, value in raw_results.items()
                if isinstance(variant, str) and isinstance(value, dict)
            }
            if isinstance(raw_results, dict)
            else {}
        )
        if not variants:
            variants = list(results)
        if not variants:
            legacy = payload.get("execution_variant", payload.get("autopilot"))
            if isinstance(legacy, str) and legacy:
                variants = [legacy]
                results = {legacy: {"status": payload.get("status", "completed")}}
        if not variants:
            raise RuntimeArtifactError(
                "runner run.json does not declare execution variants"
            )
        mismatch = mode != run.execution_mode or set(variants) != set(run.variants)
        legacy_compare_mismatch = run.execution_mode == "compare" and mismatch
        if (strict and mismatch) or (not strict and legacy_compare_mismatch):
            raise RuntimeArtifactError(
                "runner run.json does not match selected execution capabilities"
            )
        return mode, variants, results

    @staticmethod
    def _status(
        payload: dict[str, Any],
        results: dict[str, dict[str, object]],
        process_exit_code: int,
    ) -> str:
        status = payload.get("status")
        if status in {"failed", "interrupted"}:
            return status
        result_statuses = {result.get("status") for result in results.values()}
        if "failed" in result_statuses:
            return "failed"
        if "interrupted" in result_statuses:
            return "interrupted"
        if status == "completed":
            return "completed"
        return "completed" if process_exit_code == 0 else "failed"

    @staticmethod
    def _structured_error(
        payload: dict[str, Any],
        results: dict[str, dict[str, object]],
        status: str,
    ) -> str | None:
        error = payload.get("error")
        if isinstance(error, str) and error.strip():
            return error.strip()
        for variant, result in results.items():
            result_status = result.get("status")
            result_error = result.get("error")
            if result_status in {"failed", "interrupted"}:
                if isinstance(result_error, str) and result_error.strip():
                    return f"{variant}: {result_error.strip()}"
                return f"{variant}: Runtime reported {result_status}"
        if status != "completed":
            return f"Runtime reported {status}"
        return None

    def _artifact_path(self, plan: RunExecutionPlan, artifact_type: str) -> Path:
        return self.files.path(
            plan.output_directory,
            plan.artifact_manifest.path_for(artifact_type),
        )

    @staticmethod
    def _optional_number(value: object) -> float | None:
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            return float(value)
        return None
