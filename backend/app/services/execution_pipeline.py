from __future__ import annotations

from collections.abc import Sequence
from datetime import datetime, timezone
from typing import Protocol

from app.domain.pipeline import PipelineStage, PipelineStageStatus

RUN_PIPELINE: tuple[tuple[str, str], ...] = (
    ("resolve_scenario", "Resolve Scenario"),
    ("resolve_runtime_revision", "Resolve Runtime Revision"),
    ("validate_contract", "Validate Contract"),
    ("resolve_build", "Resolve Build"),
    ("freeze_scenario", "Freeze Scenario"),
    ("launch_runner", "Launch Runner"),
    ("record_telemetry", "Record Telemetry"),
    ("collect_artifacts", "Collect Artifacts"),
    ("complete", "Complete"),
)

BUILD_PIPELINE: tuple[tuple[str, str], ...] = (
    ("fetch_repository", "Fetch Repository"),
    ("prepare_worktree", "Prepare Worktree"),
    ("configure", "Configure"),
    ("compile", "Compile"),
    ("verify_artifact", "Verify Artifact"),
    ("complete", "Complete"),
)


class PipelineEntity(Protocol):
    stages: list[PipelineStage]


class PipelineStore(Protocol):
    def get(self, entity_id: int) -> PipelineEntity: ...

    def set_pipeline(
        self,
        entity_id: int,
        *,
        current_stage: str | None,
        stages: Sequence[PipelineStage],
    ) -> None: ...


class ExecutionPipelineRecorder:
    """Persist ordered, observable stage transitions for one Run or Build."""

    def __init__(
        self,
        store: PipelineStore,
        entity_id: int,
        definition: Sequence[tuple[str, str]],
    ) -> None:
        self.store = store
        self.entity_id = entity_id
        self.definition = tuple(definition)

    def initialize(self) -> None:
        entity = self.store.get(self.entity_id)
        if entity.stages:
            return
        stages = [PipelineStage(id=stage_id, label=label) for stage_id, label in self.definition]
        self.store.set_pipeline(
            self.entity_id,
            current_stage=stages[0].id if stages else None,
            stages=stages,
        )

    def running(self, stage_id: str, message: str | None = None) -> None:
        self._transition(stage_id, PipelineStageStatus.RUNNING, message=message)

    def success(self, stage_id: str, message: str | None = None) -> None:
        self._transition(stage_id, PipelineStageStatus.SUCCESS, message=message)

    def skipped(self, stage_id: str, message: str | None = None) -> None:
        self._transition(stage_id, PipelineStageStatus.SKIPPED, message=message)

    def failed(self, stage_id: str, error: str) -> None:
        self.initialize()
        stages = [stage.model_copy(deep=True) for stage in self.store.get(self.entity_id).stages]
        now = datetime.now(timezone.utc)
        index = self._index(stages, stage_id)
        stage = stages[index]
        started = stage.started_at or now
        stages[index] = stage.model_copy(
            update={
                "status": PipelineStageStatus.FAILED,
                "started_at": started,
                "finished_at": now,
                "duration_sec": max(0.0, (now - started).total_seconds()),
                "error": error[:4000],
            }
        )
        for later_index in range(index + 1, len(stages)):
            later = stages[later_index]
            if later.status is PipelineStageStatus.PENDING:
                stages[later_index] = later.model_copy(
                    update={
                        "status": PipelineStageStatus.SKIPPED,
                        "finished_at": now,
                        "message": "Not reached after an earlier stage failed",
                    }
                )
        self.store.set_pipeline(
            self.entity_id, current_stage=stage_id, stages=stages
        )

    def recoverable_failure(self, stage_id: str, error: str) -> None:
        """Record a failed stage while allowing a later lifecycle stage to finish."""
        self.initialize()
        stages = [
            stage.model_copy(deep=True)
            for stage in self.store.get(self.entity_id).stages
        ]
        now = datetime.now(timezone.utc)
        index = self._index(stages, stage_id)
        stage = stages[index]
        started = stage.started_at or now
        stages[index] = stage.model_copy(
            update={
                "status": PipelineStageStatus.FAILED,
                "started_at": started,
                "finished_at": now,
                "duration_sec": max(0.0, (now - started).total_seconds()),
                "error": error[:4000],
            }
        )
        current_stage = next(
            (
                later.id
                for later in stages[index + 1 :]
                if later.status is PipelineStageStatus.PENDING
            ),
            stage_id,
        )
        self.store.set_pipeline(
            self.entity_id, current_stage=current_stage, stages=stages
        )

    def fail_current(self, error: str) -> None:
        self.initialize()
        stages = self.store.get(self.entity_id).stages
        active = next(
            (stage.id for stage in stages if stage.status is PipelineStageStatus.RUNNING),
            None,
        )
        current = active or next(
            (stage.id for stage in stages if stage.status is PipelineStageStatus.PENDING),
            stages[-1].id if stages else None,
        )
        if current is not None:
            self.failed(current, error)

    def _transition(
        self,
        stage_id: str,
        status: PipelineStageStatus,
        *,
        message: str | None,
    ) -> None:
        self.initialize()
        stages = [stage.model_copy(deep=True) for stage in self.store.get(self.entity_id).stages]
        now = datetime.now(timezone.utc)
        index = self._index(stages, stage_id)
        stage = stages[index]
        if status is PipelineStageStatus.RUNNING:
            update = {
                "status": status,
                "started_at": stage.started_at or now,
                "finished_at": None,
                "duration_sec": None,
                "message": message,
                "error": None,
            }
        else:
            started = stage.started_at or now
            update = {
                "status": status,
                "started_at": started,
                "finished_at": now,
                "duration_sec": max(0.0, (now - started).total_seconds()),
                "message": message,
                "error": None,
            }
        stages[index] = stage.model_copy(update=update)
        if status is PipelineStageStatus.RUNNING:
            current_stage = stage_id
        else:
            current_stage = next(
                (
                    later.id
                    for later in stages[index + 1 :]
                    if later.status is PipelineStageStatus.PENDING
                ),
                stage_id,
            )
        self.store.set_pipeline(
            self.entity_id, current_stage=current_stage, stages=stages
        )

    @staticmethod
    def _index(stages: Sequence[PipelineStage], stage_id: str) -> int:
        for index, stage in enumerate(stages):
            if stage.id == stage_id:
                return index
        raise KeyError(f"unknown pipeline stage: {stage_id}")
