from __future__ import annotations

import asyncio
import json
from collections.abc import Callable
from datetime import UTC, datetime
from pathlib import Path

import pytest
from app.analysis.mcap_reader import McapRunReader
from app.domain.build import Build, BuildStatus
from app.domain.execution import RunnerResult
from app.domain.models import RunStatus
from app.domain.runtime import BuildKey
from app.infrastructure.build import BuildWorkspaceStore
from app.infrastructure.filesystem import RunArtifactStore
from app.repositories.database import Database
from app.repositories.runs import RunRepository
from app.services.artifact_ingestion import RunArtifactIngestionService
from app.services.build_manager import BuildManager
from app.services.execution import RunExecutionService
from app.services.execution_pipeline import RUN_PIPELINE, ExecutionPipelineRecorder
from app.services.execution_plan import RunExecutionPlan
from app.services.runtime_contract import (
    RuntimeArtifactDefinition,
    RuntimeArtifactManifest,
)
from app.services.telemetry_processing import RunTelemetryProcessor
from jsonschema import Draft202012Validator

from tests.conftest import FakeSimulationRunner


def execution_fixture(tmp_path: Path):
    migrations = Path(__file__).resolve().parents[1] / "migrations"
    database = Database(tmp_path / "data" / "test.db", migrations)
    database.initialize()
    runs = RunRepository(database)
    run = runs.create(
        commit_sha="a" * 40,
        scenario_name="Frozen scenario",
        scenario_path="pending",
        autopilot="primary",
        execution_variant="primary",
    )
    output = tmp_path / "data" / "runs" / f"{run.id:06d}"
    output.mkdir(parents=True)
    scenario = output / "scenario.yaml"
    scenario.write_text("name: Frozen scenario\nautopilot: primary\n", encoding="utf-8")
    artifact_store = RunArtifactStore(tmp_path / "data")
    scenario_artifact = artifact_store.metadata("scenario", scenario)
    runs.finalize_preparation(
        run.id,
        scenario_path=scenario_artifact.relative_path,
        scenario_sha256=scenario_artifact.sha256,
        output_directory=f"runs/{run.id:06d}",
        parameter_snapshot_path=None,
        parameter_snapshot_sha256=None,
        artifacts=[scenario_artifact],
    )
    pipeline = ExecutionPipelineRecorder(runs, run.id, RUN_PIPELINE)
    pipeline.initialize()
    for stage_id, _ in RUN_PIPELINE[:5]:
        pipeline.success(stage_id)
    return runs, run.id


class CountingRunner(FakeSimulationRunner):
    def __init__(self) -> None:
        super().__init__()
        self.calls = 0

    async def run(self, **kwargs) -> RunnerResult:
        self.calls += 1
        await asyncio.sleep(0)
        return await super().run(**kwargs)


class StructuredFailureRunner:
    async def run(
        self,
        *,
        argv,
        stdout_path: Path,
        stderr_path: Path,
        executable_path: Path | None = None,
        cwd: Path | None = None,
        on_started: Callable[[int], None] | None = None,
    ) -> RunnerResult:
        arguments = list(argv)
        output_directory = Path(arguments[arguments.index("--output") + 1])
        stdout_path.write_text("diagnostic only\n", encoding="utf-8")
        (output_directory / "run.json").write_text(
            json.dumps(
                {
                    "mode": "single",
                    "execution": {"variants": ["primary"]},
                    "status": "failed",
                    "error": "trim diverged",
                    "results": {
                        "primary": {
                            "status": "failed",
                            "error": "controller divergence",
                        }
                    },
                }
            ),
            encoding="utf-8",
        )
        return RunnerResult(exit_code=9, wall_time_sec=0.1)


class BrokenAnalysis:
    def process(self, *args, **kwargs):
        raise ValueError("telemetry decode failed")


class BrokenOutputRunner:
    def __init__(self, content: str | None) -> None:
        self.content = content

    async def run(self, **kwargs) -> RunnerResult:
        arguments = list(kwargs["argv"])
        output_directory = Path(arguments[arguments.index("--output") + 1])
        if self.content is not None:
            (output_directory / "run.json").write_text(self.content, encoding="utf-8")
        return RunnerResult(exit_code=0, wall_time_sec=0.1)


@pytest.mark.asyncio
async def test_duplicate_execution_claim_runs_process_once(tmp_path: Path) -> None:
    runs, run_id = execution_fixture(tmp_path)
    runner = CountingRunner()
    service = RunExecutionService(
        runs,
        runner,
        RunTelemetryProcessor(McapRunReader()),
        tmp_path / "data",
    )

    await asyncio.gather(service.execute(run_id), service.execute(run_id))

    assert runner.calls == 1
    assert runs.get(run_id).status is RunStatus.COMPLETED


@pytest.mark.asyncio
async def test_structured_runtime_failure_precedes_process_exit_diagnostic(
    tmp_path: Path,
) -> None:
    runs, run_id = execution_fixture(tmp_path)
    service = RunExecutionService(
        runs,
        StructuredFailureRunner(),
        RunTelemetryProcessor(McapRunReader()),
        tmp_path / "data",
    )

    await service.execute(run_id)

    failed = runs.get(run_id)
    assert failed.status is RunStatus.FAILED
    assert failed.exit_code == 9
    assert failed.error_message == "trim diverged"
    assert "exited with code" not in failed.error_message


@pytest.mark.asyncio
async def test_analysis_failure_does_not_reclassify_successful_simulation(
    tmp_path: Path,
) -> None:
    runs, run_id = execution_fixture(tmp_path)
    service = RunExecutionService(
        runs,
        FakeSimulationRunner(),
        BrokenAnalysis(),  # type: ignore[arg-type]
        tmp_path / "data",
    )

    await service.execute(run_id)

    completed = runs.get(run_id)
    stages = {stage.id: stage for stage in completed.stages}
    assert completed.status is RunStatus.COMPLETED
    assert completed.error_message == "analysis failed: telemetry decode failed"
    assert stages["collect_artifacts"].status.value == "failed"
    assert stages["complete"].status.value == "success"


@pytest.mark.parametrize(
    ("content", "expected"),
    [
        (None, "runner completed without run.json"),
        ("{", "runner produced an invalid run.json"),
    ],
)
@pytest.mark.asyncio
async def test_missing_or_invalid_runtime_manifest_fails_ingestion(
    tmp_path: Path, content: str | None, expected: str
) -> None:
    runs, run_id = execution_fixture(tmp_path)
    service = RunExecutionService(
        runs,
        BrokenOutputRunner(content),
        RunTelemetryProcessor(McapRunReader()),
        tmp_path / "data",
    )

    await service.execute(run_id)

    failed = runs.get(run_id)
    assert failed.status is RunStatus.FAILED
    assert failed.error_message == expected


def test_completed_build_cache_requires_consistent_metadata_and_executable(
    tmp_path: Path,
) -> None:
    workspace = BuildWorkspaceStore(tmp_path / "builds", Path("jsb-sim-runner"))
    build_dir, stdout_path, stderr_path = workspace.paths_for_id(7)
    directory = workspace.resolve(Path(build_dir))
    directory.mkdir(parents=True)
    executable = directory / "jsb-sim-runner"
    executable.write_text("#!/bin/sh\n", encoding="utf-8")
    executable.chmod(0o755)
    now = datetime.now(UTC)
    build = Build(
        id=7,
        repository_id=3,
        commit_sha="b" * 40,
        status=BuildStatus.COMPLETED,
        build_dir=build_dir,
        executable_path=workspace.relative(executable),
        stdout_path=stdout_path,
        stderr_path=stderr_path,
        created_at=now,
        completed_at=now,
    )
    key = BuildKey(repository_id=3, commit_sha="b" * 40)

    assert workspace.validate_cached(build, key) == executable
    assert (
        workspace.validate_cached(build, BuildKey(repository_id=3, commit_sha="c" * 40))
        is None
    )
    executable.unlink()
    assert workspace.validate_cached(build, key) is None


def test_require_runnable_rejects_inconsistent_completed_cache(
    tmp_path: Path,
) -> None:
    workspace = BuildWorkspaceStore(tmp_path / "builds", Path("jsb-sim-runner"))
    build_dir, stdout_path, stderr_path = workspace.paths_for_id(7)
    directory = workspace.resolve(Path(build_dir))
    directory.mkdir(parents=True)
    executable = directory / "jsb-sim-runner"
    executable.write_text("#!/bin/sh\n", encoding="utf-8")
    executable.chmod(0o755)
    now = datetime.now(UTC)
    build = Build(
        id=7,
        repository_id=3,
        commit_sha="b" * 40,
        status=BuildStatus.COMPLETED,
        build_dir=build_dir,
        executable_path=workspace.relative(executable),
        stdout_path=stdout_path,
        stderr_path=stderr_path,
        created_at=now,
        completed_at=now,
    )

    class BuildRecords:
        @staticmethod
        def get(build_id: int) -> Build:
            assert build_id == build.id
            return build.model_copy(update={"stdout_path": str(tmp_path / "wrong.log")})

    manager = object.__new__(BuildManager)
    manager.repository = BuildRecords()
    manager.workspace = workspace

    with pytest.raises(RuntimeError, match="cache metadata or executable is invalid"):
        manager.require_runnable(build.id)


def test_artifact_ingestion_uses_exact_manifest_paths(tmp_path: Path) -> None:
    runs, run_id = execution_fixture(tmp_path)
    run = runs.get(run_id)
    files = RunArtifactStore(tmp_path / "data")
    output = tmp_path / "data" / "runs" / f"{run_id:06d}"
    scenario = output / "inputs" / "frozen.yml"
    scenario.parent.mkdir(parents=True)
    scenario.write_bytes(files.resolve(run.scenario_path).read_bytes())
    metadata = output / "meta" / "runtime-result.json"
    metadata.parent.mkdir(parents=True)
    metadata.write_text(
        json.dumps(
            {
                "runtime": {"commit": run.commit_sha},
                "execution": {"mode": "single", "variants": ["primary"]},
                "status": "completed",
                "results": {"primary": {"status": "completed"}},
            }
        ),
        encoding="utf-8",
    )
    telemetry = output / "records" / "flight-data.mcap"
    telemetry.parent.mkdir(parents=True)
    telemetry.write_bytes(b"mcap")
    manifest = RuntimeArtifactManifest(
        schema_version=1,
        artifacts=(
            RuntimeArtifactDefinition(
                "run_metadata",
                "meta/runtime-result.json",
                True,
                "application/json",
            ),
            RuntimeArtifactDefinition(
                "scenario_snapshot",
                "inputs/frozen.yml",
                True,
                "application/yaml",
            ),
            RuntimeArtifactDefinition(
                "telemetry",
                "records/flight-data.mcap",
                True,
                "application/x-mcap",
            ),
        ),
    )
    plan = RunExecutionPlan(
        run_id=run_id,
        repository_id=None,
        commit_sha=run.commit_sha,
        build_id=None,
        executable=None,
        argv=(),
        cwd=None,
        scenario_snapshot=scenario,
        parameter_snapshot=None,
        output_directory=output,
        stdout_log=output / "stdout.log",
        stderr_log=output / "stderr.log",
        artifact_manifest=manifest,
        contract_version="2.0.0",
        signal_catalog=None,
        telemetry_descriptor=None,
        run_schema=Draft202012Validator({"type": "object"}),
        execution_mode="single",
        variants=("primary",),
        scenario_sha256=None,
    )

    outcome = RunArtifactIngestionService(runs, files).ingest(
        run, plan, process_exit_code=0
    )

    assert outcome.succeeded
    assert outcome.telemetry_path == telemetry
    artifacts = {item["kind"]: item["path"] for item in runs.get_artifact_rows(run_id)}
    assert artifacts["run"].endswith("meta/runtime-result.json")
    assert artifacts["telemetry"].endswith("records/flight-data.mcap")
