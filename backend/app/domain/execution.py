from __future__ import annotations

from dataclasses import dataclass
from pathlib import PurePosixPath

from app.domain.artifacts import StoredArtifact


@dataclass(frozen=True)
class RunnerResult:
    exit_code: int
    wall_time_sec: float


@dataclass(frozen=True)
class FrozenRunPreparation:
    run_id: int
    scenario_path: str
    scenario_sha256: str
    output_directory: str
    parameter_snapshot_path: str | None
    parameter_snapshot_sha256: str | None
    artifacts: tuple[StoredArtifact, ...]

    def __post_init__(self) -> None:
        for value in (self.scenario_path, self.output_directory):
            path = PurePosixPath(value)
            if not value or path.is_absolute() or ".." in path.parts or "\\" in value:
                raise ValueError("Run preparation paths must be relative")
        output = PurePosixPath(self.output_directory)
        if not PurePosixPath(self.scenario_path).is_relative_to(output):
            raise ValueError(
                "scenario snapshot must be inside the Run output directory"
            )
        scenario = next(
            (artifact for artifact in self.artifacts if artifact.kind == "scenario"),
            None,
        )
        if (
            scenario is None
            or scenario.relative_path != self.scenario_path
            or scenario.sha256 != self.scenario_sha256
        ):
            raise ValueError("scenario artifact metadata does not match Run provenance")
        has_parameter_path = self.parameter_snapshot_path is not None
        if has_parameter_path != (self.parameter_snapshot_sha256 is not None):
            raise ValueError(
                "parameter snapshot path and digest must be provided together"
            )
        if has_parameter_path:
            parameter_path = PurePosixPath(self.parameter_snapshot_path or "")
            if not parameter_path.is_relative_to(output):
                raise ValueError(
                    "parameter snapshot must be inside the Run output directory"
                )
            parameters = next(
                (
                    artifact
                    for artifact in self.artifacts
                    if artifact.kind == "parameters"
                ),
                None,
            )
            if (
                parameters is None
                or parameters.relative_path != self.parameter_snapshot_path
                or parameters.sha256 != self.parameter_snapshot_sha256
            ):
                raise ValueError(
                    "parameter artifact metadata does not match Run provenance"
                )
