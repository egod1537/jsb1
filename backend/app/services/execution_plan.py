from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from jsonschema import Draft202012Validator

from app.domain.models import Run
from app.domain.telemetry import RuntimeSignalCatalog
from app.infrastructure.filesystem import RunArtifactStore
from app.services.build_manager import BuildManager
from app.services.repository_manager import RepositoryManager
from app.services.runtime_contract import RuntimeArtifactManifest, RuntimeContractReader


@dataclass(frozen=True)
class RunExecutionPlan:
    run_id: int
    repository_id: int | None
    commit_sha: str | None
    build_id: int | None
    executable: Path | None
    argv: tuple[str, ...]
    cwd: Path | None
    scenario_snapshot: Path
    parameter_snapshot: Path | None
    output_directory: Path
    stdout_log: Path
    stderr_log: Path
    artifact_manifest: RuntimeArtifactManifest
    contract_version: str | None
    signal_catalog: RuntimeSignalCatalog | None
    telemetry_descriptor: bytes | None
    run_schema: Draft202012Validator | None
    execution_mode: str
    variants: tuple[str, ...]
    scenario_sha256: str | None


class RunExecutionPlanner:
    """Build one immutable execution input from already-frozen Run provenance."""

    def __init__(
        self,
        artifacts: RunArtifactStore,
        builds: BuildManager | None,
        repositories: RepositoryManager | None,
        contracts: RuntimeContractReader,
    ) -> None:
        self.artifacts = artifacts
        self.builds = builds
        self.repositories = repositories
        self.contracts = contracts

    def create(self, run: Run) -> RunExecutionPlan:
        if not run.output_directory:
            raise RuntimeError("run output directory was not initialized")
        output = self.artifacts.prepare_output(run.output_directory)

        contract_version: str | None = None
        signal_catalog: RuntimeSignalCatalog | None = None
        descriptor: bytes | None = None
        run_schema: Draft202012Validator | None = None
        manifest = self.contracts.legacy_artifact_manifest()
        exact_revision = run.branch is not None
        if exact_revision:
            if run.repository_id is None or not run.commit_sha:
                raise RuntimeError("run has incomplete immutable Runtime provenance")
            if self.repositories is None:
                raise RuntimeError("Runtime repository service is not configured")
            worktree = self.repositories.prepare_worktree(
                run.repository_id, run.commit_sha
            )
            if self.contracts.is_indexed(worktree):
                bundle = self.contracts.load_bundle(
                    worktree,
                    repository_id=run.repository_id,
                    commit_sha=run.commit_sha,
                )
                manifest = bundle.artifact_manifest
                contract_version = bundle.version
                signal_catalog = bundle.signal_catalog
                descriptor = bundle.telemetry_descriptor
                run_schema = bundle.run_schema
                if run.contract_version != bundle.version:
                    raise RuntimeError(
                        "run contract version does not match immutable Runtime provenance"
                    )
            else:
                manifest = self.contracts.load_artifact_manifest(worktree)

        executable: Path | None = None
        if run.build_id is not None:
            if self.builds is None:
                raise RuntimeError("build service is not configured")
            build, executable = self.builds.require_runnable(run.build_id)
            if (
                run.repository_id is not None
                and build.repository_id != run.repository_id
            ):
                raise RuntimeError("run and build repository provenance do not match")
            if run.commit_sha is not None and build.commit_sha != run.commit_sha:
                raise RuntimeError("run and build commit provenance do not match")
        elif exact_revision:
            raise RuntimeError("immutable Runtime run has no reserved build")

        expected_scenario = self.artifacts.path(
            output, manifest.path_for("scenario_snapshot")
        )
        scenario = self.artifacts.resolve(run.scenario_path)
        if scenario != expected_scenario or not self.artifacts.is_file(scenario):
            raise RuntimeError("run scenario snapshot does not match frozen provenance")
        if (
            run.scenario_sha256
            and self.artifacts.metadata("scenario", scenario).sha256
            != run.scenario_sha256
        ):
            raise RuntimeError("run scenario snapshot digest does not match provenance")

        parameter_snapshot: Path | None = None
        if run.controller_parameter_overrides:
            expected_parameter_snapshot = self.artifacts.path(
                output, manifest.path_for("parameter_set_snapshot")
            )
            if not run.parameter_snapshot_path:
                raise RuntimeError("run parameter snapshot provenance is missing")
            parameter_snapshot = self.artifacts.resolve(run.parameter_snapshot_path)
            if parameter_snapshot != expected_parameter_snapshot:
                raise RuntimeError(
                    "run parameter snapshot does not match frozen provenance"
                )
            if not self.artifacts.is_file(parameter_snapshot):
                raise RuntimeError("run parameter snapshot is missing")
            if (
                not run.parameter_snapshot_sha256
                or self.artifacts.metadata("parameters", parameter_snapshot).sha256
                != run.parameter_snapshot_sha256
            ):
                raise RuntimeError(
                    "run parameter snapshot digest does not match provenance"
                )

        return RunExecutionPlan(
            run_id=run.id,
            repository_id=run.repository_id,
            commit_sha=run.commit_sha,
            build_id=run.build_id,
            executable=executable,
            argv=(
                "--scenario",
                str(scenario),
                "--output",
                str(output),
            ),
            cwd=executable.parent if executable is not None else None,
            scenario_snapshot=scenario,
            parameter_snapshot=parameter_snapshot,
            output_directory=output,
            stdout_log=self.artifacts.path(output, "stdout.log"),
            stderr_log=self.artifacts.path(output, "stderr.log"),
            artifact_manifest=manifest,
            contract_version=contract_version,
            signal_catalog=signal_catalog,
            telemetry_descriptor=descriptor,
            run_schema=run_schema,
            execution_mode=run.execution_mode,
            variants=tuple(run.variants),
            scenario_sha256=run.scenario_sha256,
        )
