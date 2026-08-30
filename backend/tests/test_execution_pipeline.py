from __future__ import annotations

from dataclasses import dataclass, field

from app.domain.pipeline import PipelineStage, PipelineStageStatus
from app.services.execution_pipeline import ExecutionPipelineRecorder, RUN_PIPELINE


@dataclass
class Entity:
    stages: list[PipelineStage] = field(default_factory=list)
    current_stage: str | None = None


class MemoryPipelineStore:
    def __init__(self) -> None:
        self.entity = Entity()

    def get(self, _entity_id: int) -> Entity:
        return self.entity

    def set_pipeline(
        self,
        _entity_id: int,
        *,
        current_stage: str | None,
        stages: list[PipelineStage],
    ) -> None:
        self.entity = Entity(
            stages=[stage.model_copy(deep=True) for stage in stages],
            current_stage=current_stage,
        )


def test_pipeline_pending_running_success_transitions_are_persisted() -> None:
    store = MemoryPipelineStore()
    recorder = ExecutionPipelineRecorder(store, 1, RUN_PIPELINE)

    recorder.initialize()
    assert store.entity.current_stage == "resolve_scenario"
    assert all(stage.status is PipelineStageStatus.PENDING for stage in store.entity.stages)

    recorder.running("resolve_scenario", "Loading bundled scenario")
    running = store.entity.stages[0]
    assert running.status is PipelineStageStatus.RUNNING
    assert running.started_at is not None
    assert running.message == "Loading bundled scenario"

    recorder.success("resolve_scenario")
    completed = store.entity.stages[0]
    assert completed.status is PipelineStageStatus.SUCCESS
    assert completed.finished_at is not None
    assert completed.duration_sec is not None
    assert store.entity.current_stage == "resolve_runtime_revision"


def test_pipeline_failure_marks_remaining_stages_skipped() -> None:
    store = MemoryPipelineStore()
    recorder = ExecutionPipelineRecorder(store, 1, RUN_PIPELINE)
    recorder.initialize()
    recorder.success("resolve_scenario")
    recorder.running("resolve_runtime_revision")

    recorder.fail_current("branch not found")

    revision = store.entity.stages[1]
    assert revision.status is PipelineStageStatus.FAILED
    assert revision.error == "branch not found"
    assert store.entity.current_stage == "resolve_runtime_revision"
    assert all(
        stage.status is PipelineStageStatus.SKIPPED
        for stage in store.entity.stages[2:]
    )
