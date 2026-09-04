from __future__ import annotations

import asyncio
import logging
from pathlib import Path

from app.domain.clock import utc_now
from app.domain.errors import RunAnalysisError, RuntimeReportedFailure
from app.domain.models import Run
from app.infrastructure.filesystem import RunArtifactStore
from app.repositories.instances import InstanceRepository
from app.repositories.runs import RunRepository
from app.services.artifact_ingestion import (
    RunArtifactIngestionService,
    RuntimeManifestOutcome,
)
from app.services.build_manager import BuildManager
from app.services.execution_pipeline import RUN_PIPELINE, ExecutionPipelineRecorder
from app.services.execution_plan import RunExecutionPlan, RunExecutionPlanner
from app.services.ports import SimulationRunner
from app.services.repository_manager import RepositoryManager
from app.services.runtime_contract import RuntimeContractReader
from app.services.telemetry_processing import RunTelemetryProcessor

logger = logging.getLogger(__name__)


def now_iso() -> str:
    return utc_now()


class RunExecutionService:
    """Claim and execute a Run using only its frozen provenance."""

    def __init__(
        self,
        repository: RunRepository,
        runner: SimulationRunner,
        telemetry: RunTelemetryProcessor,
        data_dir: Path,
        builds: BuildManager | None = None,
        instances: InstanceRepository | None = None,
        repositories: RepositoryManager | None = None,
        contract_reader: RuntimeContractReader | None = None,
        artifact_store: RunArtifactStore | None = None,
        planner: RunExecutionPlanner | None = None,
        ingestion: RunArtifactIngestionService | None = None,
    ) -> None:
        self.repository = repository
        self.runner = runner
        self.telemetry = telemetry
        self.data_dir = data_dir.resolve()
        self.artifact_store = artifact_store or RunArtifactStore(self.data_dir)
        self.builds = builds
        self.instances = instances
        self.repositories = repositories
        self.contract_reader = contract_reader or RuntimeContractReader()
        self.planner = planner or RunExecutionPlanner(
            self.artifact_store,
            builds,
            repositories,
            self.contract_reader,
        )
        self.ingestion = ingestion or RunArtifactIngestionService(
            repository, self.artifact_store
        )

    async def execute(self, run_id: int) -> None:
        run = self.repository.claim_for_execution(run_id, started_at=now_iso())
        if run is None:
            logger.info("run claim skipped id=%s", run_id)
            return

        pipeline = ExecutionPipelineRecorder(self.repository, run_id, RUN_PIPELINE)
        pipeline.initialize()
        instance = self.instances.get_for_run(run_id) if self.instances else None
        result = None
        plan: RunExecutionPlan | None = None
        try:
            plan = await asyncio.to_thread(self.planner.create, run)
            logger.info(
                "run started id=%s commit=%s scenario=%s",
                run.id,
                plan.commit_sha,
                run.scenario_name,
            )
            pipeline.running("launch_runner")

            def on_started(pid: int) -> None:
                if instance is not None and self.instances is not None:
                    self.instances.mark_running(instance.id, pid)

            result = await self.runner.run(
                argv=plan.argv,
                stdout_path=plan.stdout_log,
                stderr_path=plan.stderr_log,
                executable_path=plan.executable,
                cwd=plan.cwd,
                on_started=on_started,
            )
            self._register_diagnostic_logs(run_id, plan)
            manifest_path = self.artifact_store.path(
                plan.output_directory,
                plan.artifact_manifest.path_for("run_metadata"),
            )
            if result.exit_code != 0 and not self.artifact_store.is_file(manifest_path):
                raise RuntimeReportedFailure(
                    f"runner exited with code {result.exit_code}"
                )
            pipeline.success("launch_runner")

            pipeline.running("record_telemetry")
            outcome = self.ingestion.ingest(
                run, plan, process_exit_code=result.exit_code
            )
            if not outcome.succeeded:
                raise RuntimeReportedFailure(
                    outcome.error or f"Runtime reported {outcome.status}"
                )
            if result.exit_code != 0:
                raise RuntimeReportedFailure(
                    f"runner exited with code {result.exit_code}"
                )
            if outcome.telemetry_path is None:
                raise RuntimeError("completed Runtime did not publish telemetry")
            pipeline.success("record_telemetry")

            self._analyze_and_complete(
                run_id,
                run,
                plan,
                outcome,
                result.exit_code,
                result.wall_time_sec,
                instance.id if instance is not None else None,
                pipeline,
            )
        except asyncio.CancelledError:
            pipeline.fail_current("execution worker stopped while run was active")
            await self._fail(
                run_id,
                "execution worker stopped while run was active",
                exit_code=getattr(result, "exit_code", None),
                wall_time=getattr(result, "wall_time_sec", None),
            )
            raise
        except Exception as exc:
            logger.exception("run failed id=%s", run_id)
            pipeline.fail_current(str(exc))
            await self._fail(
                run_id,
                str(exc),
                exit_code=getattr(result, "exit_code", None),
                wall_time=getattr(result, "wall_time_sec", None),
            )
        finally:
            if plan is not None:
                self._register_diagnostic_logs(run_id, plan)

    def _analyze_and_complete(
        self,
        run_id: int,
        run: Run,
        plan: RunExecutionPlan,
        outcome: RuntimeManifestOutcome,
        exit_code: int,
        wall_time_sec: float,
        instance_id: int | None,
        pipeline: ExecutionPipelineRecorder,
    ) -> None:
        assert outcome.telemetry_path is not None
        simulation_time = outcome.simulation_time_sec
        analysis_error: str | None = None
        pipeline.running("collect_artifacts")
        try:
            metrics_path = self.artifact_store.path(
                plan.output_directory, "metrics.json"
            )
            processed = self.telemetry.process(
                outcome.telemetry_path,
                metrics_path,
                variants=list(plan.variants),
                signal_catalog=plan.signal_catalog,
                descriptor=plan.telemetry_descriptor,
            )
            simulation_time = processed.simulation_time_sec
            self.repository.replace_metrics(run_id, processed.metrics)
            self.artifact_store.write_json(metrics_path, processed.metrics_payload)
            self._register(run_id, "metrics", metrics_path)
            pipeline.success("collect_artifacts")
            logger.info("analysis completed run_id=%s", run_id)
        except Exception as exc:
            detail = str(exc).strip() or exc.__class__.__name__
            failure = RunAnalysisError(detail)
            analysis_error = f"analysis failed: {failure}"
            logger.exception(
                "analysis failed for completed simulation run_id=%s", run_id
            )
            pipeline.recoverable_failure("collect_artifacts", analysis_error)

        pipeline.running("complete")
        self.repository.complete_run(
            run_id,
            finished_at=now_iso(),
            exit_code=exit_code,
            wall_time_sec=wall_time_sec,
            simulation_time_sec=simulation_time,
            error_message=analysis_error,
        )
        pipeline.success(
            "complete",
            "Simulation completed; analysis unavailable"
            if analysis_error
            else "Simulation and analysis completed",
        )
        self._write_provenance(run_id, plan.output_directory)
        if instance_id is not None and self.instances is not None:
            self.instances.finish(instance_id, failed=False)

    async def _fail(
        self,
        run_id: int,
        message: str,
        *,
        exit_code: int | None = None,
        wall_time: float | None = None,
    ) -> None:
        try:
            self.repository.fail_run(
                run_id,
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
        run = self.repository.get(run_id)
        if run.output_directory:
            try:
                output = self.artifact_store.prepare_output(run.output_directory)
                self._write_provenance(run_id, output)
            except Exception:
                logger.exception(
                    "could not finalize failed run artifacts run_id=%s", run_id
                )

    def _register_diagnostic_logs(self, run_id: int, plan: RunExecutionPlan) -> None:
        for kind, path in (
            ("stdout", plan.stdout_log),
            ("stderr", plan.stderr_log),
        ):
            if self.artifact_store.is_file(path):
                self._register(run_id, kind, path)

    def _register(self, run_id: int, kind: str, path: Path) -> None:
        self.repository.record_artifact(
            run_id, self.artifact_store.metadata(kind, path)
        )

    def _write_provenance(self, run_id: int, output_directory: Path) -> None:
        run = self.repository.get(run_id)
        path = self.artifact_store.path(output_directory, "jsb1-run.json")
        payload = run.model_dump(mode="json")
        payload["repository"] = (
            {"id": run.repository_id, "name": run.repository_name}
            if run.repository_id is not None
            else None
        )
        self.artifact_store.write_json(path, payload)
        self._register(run_id, "provenance", path)
