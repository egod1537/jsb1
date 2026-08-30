from __future__ import annotations

import asyncio
import json
import logging
from datetime import datetime, timezone
from pathlib import Path

from app.domain.models import RunStatus
from app.repositories.runs import RunRepository
from app.repositories.instances import InstanceRepository
from app.services.build_manager import BuildManager
from app.services.runner import SimulationRunner
from app.services.telemetry_processing import RunTelemetryProcessor
from app.services.execution_pipeline import RUN_PIPELINE, ExecutionPipelineRecorder


logger = logging.getLogger(__name__)


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


class RunExecutionService:
    def __init__(
        self,
        repository: RunRepository,
        runner: SimulationRunner,
        telemetry: RunTelemetryProcessor,
        data_dir: Path,
        builds: BuildManager | None = None,
        instances: InstanceRepository | None = None,
    ) -> None:
        self.repository = repository
        self.runner = runner
        self.telemetry = telemetry
        self.data_dir = data_dir.resolve()
        self.builds = builds
        self.instances = instances

    async def execute(self, run_id: int) -> None:
        run = self.repository.get(run_id)
        pipeline = ExecutionPipelineRecorder(self.repository, run_id, RUN_PIPELINE)
        pipeline.initialize()
        try:
            if run.output_directory is None:
                raise RuntimeError("run output directory was not initialized")
            output_directory = (self.data_dir / run.output_directory).resolve()
            output_directory.mkdir(parents=True, exist_ok=True)
            log_path = output_directory / "stdout.log"
            logger.info("run started id=%s scenario=%s", run.id, run.scenario_name)
            self.repository.transition(
                run_id,
                expected=[RunStatus.QUEUED],
                status=RunStatus.RUNNING,
                started_at=now_iso(),
            )
            pipeline.running("launch_runner")
            executable_path: Path | None = None
            instance = self.instances.get_for_run(run_id) if self.instances else None
            if run.build_id is not None:
                if self.builds is None:
                    raise RuntimeError("build repository is not configured")
                _, executable_path = self.builds.require_runnable(run.build_id)

            def on_started(pid: int) -> None:
                if instance is not None and self.instances is not None:
                    self.instances.mark_running(instance.id, pid)

            result = await self.runner.run(
                scenario_path=Path(run.scenario_path),
                output_directory=output_directory,
                log_path=log_path,
                executable_path=executable_path,
                parameters_path=(
                    output_directory / "parameters.yaml"
                    if run.controller_parameter_overrides
                    else None
                ),
                on_started=on_started,
            )
            if log_path.exists():
                self._register(run_id, "stdout", log_path)
            if result.exit_code != 0:
                raise RuntimeError(f"runner exited with code {result.exit_code}")
            pipeline.success("launch_runner")

            runtime_manifest = output_directory / "run.json"
            self._ingest_runtime_manifest(run_id, runtime_manifest)
            self._register(run_id, "run", runtime_manifest)
            run = self.repository.get(run_id)

            pipeline.running("record_telemetry")
            telemetry_path = output_directory / "telemetry.mcap"
            if not telemetry_path.is_file():
                raise FileNotFoundError("runner completed without telemetry.mcap")
            self._register(run_id, "telemetry", telemetry_path)
            pipeline.success("record_telemetry")

            pipeline.running("collect_artifacts")
            logger.info("analysis started run_id=%s", run_id)
            metrics_path = output_directory / "metrics.json"
            processed = self.telemetry.process(
                telemetry_path, metrics_path, variants=run.variants
            )
            self.repository.replace_metrics(run_id, processed.metrics)
            self.telemetry.write_metrics(metrics_path, processed.metrics_payload)
            self._register(run_id, "metrics", metrics_path)
            pipeline.success("collect_artifacts")
            pipeline.running("complete")
            self.repository.transition(
                run_id,
                expected=[RunStatus.RUNNING],
                status=RunStatus.COMPLETED,
                finished_at=now_iso(),
                exit_code=result.exit_code,
                wall_time_sec=result.wall_time_sec,
                simulation_time_sec=processed.simulation_time_sec,
                error_message=None,
            )
            pipeline.success("complete")
            self._write_provenance(run_id)
            if instance is not None and self.instances is not None:
                self.instances.finish(instance.id, failed=False)
            logger.info("analysis completed run_id=%s", run_id)
        except asyncio.CancelledError:
            pipeline.fail_current("execution worker stopped while run was active")
            await self._fail(run_id, "execution worker stopped while run was active")
            raise
        except Exception as exc:
            logger.exception("run failed id=%s", run_id)
            pipeline.fail_current(str(exc))
            exit_code = getattr(locals().get("result"), "exit_code", None)
            wall_time = getattr(locals().get("result"), "wall_time_sec", None)
            await self._fail(run_id, str(exc), exit_code=exit_code, wall_time=wall_time)

    async def _fail(
        self,
        run_id: int,
        message: str,
        *,
        exit_code: int | None = None,
        wall_time: float | None = None,
    ) -> None:
        try:
            run = self.repository.get(run_id)
            expected = [RunStatus.QUEUED, RunStatus.RUNNING]
            self.repository.transition(
                run_id,
                expected=expected,
                status=RunStatus.FAILED,
                finished_at=now_iso(),
                exit_code=exit_code,
                wall_time_sec=wall_time,
                error_message=message[:4000],
            )
            if self.instances is not None:
                instance = self.instances.get_for_run(run_id)
                if instance is not None:
                    self.instances.finish(instance.id, failed=True)
        except Exception:
            logger.exception("could not persist failed status run_id=%s", run_id)
            return
        if run.output_directory:
            try:
                output = (self.data_dir / run.output_directory).resolve()
                output.mkdir(parents=True, exist_ok=True)
                log_path = output / "stdout.log"
                if log_path.exists():
                    self._register(run_id, "stdout", log_path)
                runtime_manifest = output / "run.json"
                if runtime_manifest.is_file():
                    self._register(run_id, "run", runtime_manifest)
                self._write_provenance(run_id)
            except Exception:
                logger.exception("could not finalize failed run artifacts run_id=%s", run_id)

    def _register(self, run_id: int, kind: str, path: Path) -> None:
        relative = path.resolve().relative_to(self.data_dir).as_posix()
        self.repository.upsert_artifact(run_id, kind, relative)

    def _write_provenance(self, run_id: int) -> None:
        run = self.repository.get(run_id)
        if not run.output_directory:
            return
        path = self.data_dir / run.output_directory / "jsb1-run.json"
        payload = run.model_dump(mode="json")
        payload["repository"] = (
            {"id": run.repository_id, "name": run.repository_name}
            if run.repository_id is not None
            else None
        )
        path.write_text(
            json.dumps(payload, indent=2, allow_nan=False), encoding="utf-8"
        )
        self._register(run_id, "provenance", path)

    def _ingest_runtime_manifest(self, run_id: int, path: Path) -> None:
        if not path.is_file():
            raise FileNotFoundError("runner completed without run.json")
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            raise ValueError("runner produced an invalid run.json") from exc
        if not isinstance(payload, dict):
            raise ValueError("runner run.json must be an object")
        execution = payload.get("execution")
        execution = execution if isinstance(execution, dict) else {}
        raw_variants = execution.get("variants", payload.get("variants"))
        variants = [
            item for item in raw_variants or []
            if isinstance(item, str) and item.strip()
        ] if isinstance(raw_variants, list) else []
        mode = payload.get("mode", execution.get("mode", "single"))
        if not isinstance(mode, str):
            mode = "single"
        mode = mode.strip().lower().replace("_", "-")
        if mode in {"compare-only", "comparison"}:
            mode = "compare"
        raw_results = payload.get("results", {})
        results = {
            variant: value
            for variant, value in raw_results.items()
            if isinstance(variant, str) and isinstance(value, dict)
        } if isinstance(raw_results, dict) else {}
        if not variants:
            variants = list(results)
        if not variants:
            legacy = payload.get("execution_variant", payload.get("autopilot"))
            if isinstance(legacy, str) and legacy:
                variants = [legacy]
                results = {legacy: {"status": payload.get("status", "completed")}}
        if not variants:
            raise ValueError("runner run.json does not declare execution variants")
        run = self.repository.get(run_id)
        if run.execution_mode == "compare" and (
            mode != "compare" or set(variants) != set(run.variants)
        ):
            raise ValueError(
                "runner run.json does not match compare-only execution capabilities"
            )
        parameters = dict(run.variant_parameters)
        for variant, result_payload in results.items():
            raw_parameters = result_payload.get("parameters")
            if isinstance(raw_parameters, dict):
                parameters[variant] = {
                    key: float(value)
                    for key, value in raw_parameters.items()
                    if isinstance(key, str)
                    and isinstance(value, (int, float))
                    and not isinstance(value, bool)
                }
        self.repository.set_variant_metadata(
            run_id,
            execution_mode=mode,
            variants=variants,
            results=results,
            parameters=parameters,
        )
