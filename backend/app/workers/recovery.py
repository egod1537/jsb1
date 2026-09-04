from __future__ import annotations

import logging
from dataclasses import dataclass

from app.domain.build import BuildStatus
from app.domain.models import RunStatus
from app.repositories.builds import BuildRepository
from app.repositories.instances import InstanceRepository
from app.repositories.runs import RunRepository
from app.services.execution_pipeline import (
    BUILD_PIPELINE,
    RUN_PIPELINE,
    ExecutionPipelineRecorder,
)

LOGGER = logging.getLogger(__name__)


@dataclass(frozen=True)
class WorkerRecoveryResult:
    builds: int
    runs: int
    instances: int


class WorkerRecoveryService:
    """Apply the explicit stale-running policy before polling resumes."""

    def __init__(
        self,
        builds: BuildRepository,
        runs: RunRepository,
        instances: InstanceRepository,
    ) -> None:
        self.builds = builds
        self.runs = runs
        self.instances = instances

    def recover(self) -> WorkerRecoveryResult:
        interrupted_build_ids = self.builds.ids_with_statuses([BuildStatus.RUNNING])
        interrupted_run_ids = self.runs.ids_with_statuses([RunStatus.RUNNING])
        result = WorkerRecoveryResult(
            builds=self.builds.fail_running_from_previous_worker(),
            runs=self.runs.fail_running_from_previous_worker(),
            instances=self.instances.fail_running_from_previous_worker(),
        )
        for build_id in interrupted_build_ids:
            ExecutionPipelineRecorder(
                self.builds, build_id, BUILD_PIPELINE
            ).fail_current("execution worker restarted during build")
        for run_id in interrupted_run_ids:
            ExecutionPipelineRecorder(self.runs, run_id, RUN_PIPELINE).fail_current(
                "execution worker restarted during run"
            )
        if result.builds or result.runs or result.instances:
            LOGGER.warning(
                "recovered interrupted worker state builds=%s runs=%s instances=%s",
                result.builds,
                result.runs,
                result.instances,
            )
        return result
