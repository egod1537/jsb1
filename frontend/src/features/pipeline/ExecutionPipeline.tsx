import { Icon, Spinner } from "@blueprintjs/core";
import { IconNames, type IconName } from "@blueprintjs/icons";
import { useId, useMemo, useState } from "react";
import type { PipelineStage, PipelineStageStatus } from "../../types/api";

export interface PipelineGroupDefinition {
  id: string;
  label: string;
  stageIds: readonly string[];
  acceptsUnmapped?: boolean;
}

export interface PipelineStageGroup {
  id: string;
  label: string;
  status: PipelineStageStatus;
  stages: PipelineStage[];
}

export const RUN_PIPELINE_GROUPS: readonly PipelineGroupDefinition[] = [
  { id: "prepare", label: "Prepare", stageIds: ["resolve_scenario", "resolve_runtime_revision", "validate_contract", "freeze_scenario"] },
  { id: "build", label: "Build", stageIds: ["resolve_build"] },
  { id: "execute", label: "Execute", stageIds: ["launch_runner"] },
  { id: "process", label: "Process", stageIds: ["record_telemetry"], acceptsUnmapped: true },
  { id: "collect", label: "Collect", stageIds: ["collect_artifacts"] },
  { id: "complete", label: "Complete", stageIds: ["complete"] },
];

export const BUILD_PIPELINE_GROUPS: readonly PipelineGroupDefinition[] = [
  { id: "prepare", label: "Prepare", stageIds: ["fetch_repository", "prepare_worktree"] },
  { id: "build", label: "Build", stageIds: ["configure", "compile"], acceptsUnmapped: true },
  { id: "verify", label: "Verify", stageIds: ["verify_artifact"] },
  { id: "complete", label: "Complete", stageIds: ["complete"] },
];

const iconByStatus: Record<Exclude<PipelineStageStatus, "running">, IconName> = {
  pending: IconNames.TIME,
  success: IconNames.TICK_CIRCLE,
  failed: IconNames.ERROR,
  skipped: IconNames.DISABLE,
};

function formatTime(value: string | null) {
  return value ? new Date(value).toLocaleString() : "—";
}

function formatDuration(value: number | null) {
  if (value == null) return "—";
  return value < 1 ? `${Math.round(value * 1000)} ms` : `${value.toFixed(2)} s`;
}

function StageIcon({ status }: { status: PipelineStageStatus }) {
  return status === "running"
    ? <Spinner size={15} aria-label="Running" />
    : <Icon icon={iconByStatus[status]} aria-hidden />;
}

export function pipelineGroupStatus(stages: PipelineStage[]): PipelineStageStatus {
  if (stages.some((stage) => stage.status === "failed")) return "failed";
  if (stages.some((stage) => stage.status === "running")) return "running";
  if (stages.length > 0 && stages.every((stage) => stage.status === "success" || stage.status === "skipped")) return "success";
  return "pending";
}

export function groupPipelineStages(
  stages: PipelineStage[],
  definitions: readonly PipelineGroupDefinition[],
): PipelineStageGroup[] {
  const stagesByGroup = new Map(definitions.map((group) => [group.id, [] as PipelineStage[]]));
  const groupByStageId = new Map(definitions.flatMap((group) => group.stageIds.map((stageId) => [stageId, group.id] as const)));
  const fallbackGroup = definitions.find((group) => group.acceptsUnmapped)?.id;

  for (const stage of stages) {
    const groupId = groupByStageId.get(stage.id) ?? fallbackGroup;
    if (groupId) stagesByGroup.get(groupId)?.push(stage);
  }

  return definitions.map((definition) => {
    const groupStages = stagesByGroup.get(definition.id) ?? [];
    return {
      id: definition.id,
      label: definition.label,
      status: pipelineGroupStatus(groupStages),
      stages: groupStages,
    };
  });
}

function DetailedStage({ stage }: { stage: PipelineStage }) {
  return <div className={`pipeline-detail-stage pipeline-detail-stage-${stage.status}`} role="listitem">
    <span className="pipeline-detail-stage-icon"><StageIcon status={stage.status} /></span>
    <div className="pipeline-detail-stage-content">
      <div className="pipeline-detail-stage-heading">
        <strong>{stage.label}</strong>
        <span>{stage.status}</span>
        <time>{formatDuration(stage.duration_sec)}</time>
      </div>
      <div className="pipeline-detail-stage-times">
        <span>Started {formatTime(stage.started_at)}</span>
        <span>Finished {formatTime(stage.finished_at)}</span>
      </div>
      {stage.message && <p>{stage.message}</p>}
      {stage.error && <p className="pipeline-stage-error" role="alert">{stage.error}</p>}
    </div>
  </div>;
}

export function ExecutionPipeline({
  stages,
  groups,
  title = "Execution pipeline",
}: {
  stages: PipelineStage[];
  groups: readonly PipelineGroupDefinition[];
  title?: string;
}) {
  const pipelineId = useId();
  const [expandedGroupId, setExpandedGroupId] = useState<string | null>(null);
  const stageGroups = useMemo(() => groupPipelineStages(stages, groups), [groups, stages]);
  const expandedGroup = stageGroups.find((group) => group.id === expandedGroupId);
  if (stages.length === 0) return null;
  return <section className="execution-pipeline panel" aria-label={title}>
    <header className="panel-header">
      <span className="panel-title">{title}</span>
      <span className="panel-count">{stageGroups.length} groups · {stages.length} stages</span>
    </header>
    <div className="pipeline-scroll" role="list" aria-label={`${title} groups`}>
      {stageGroups.map((group, index) => {
        const expanded = group.id === expandedGroupId;
        const detailsId = `${pipelineId}-pipeline-group-${group.id}`;
        return <div className="pipeline-step" key={group.id} role="listitem">
          {index > 0 && <span className={`pipeline-connector pipeline-connector-${group.status}`} aria-hidden />}
          <button
            type="button"
            className={`pipeline-node pipeline-node-${group.status}`}
            aria-label={`${group.label}: ${group.status}`}
            aria-expanded={expanded}
            aria-controls={detailsId}
            onClick={() => setExpandedGroupId(expanded ? null : group.id)}
          >
            <span className="pipeline-node-icon"><StageIcon status={group.status} /></span>
            <span className="pipeline-node-copy">
              <strong>{group.label}</strong>
              <small>{group.status} · {group.stages.length}</small>
            </span>
            <Icon className="pipeline-node-caret" icon={expanded ? IconNames.CHEVRON_UP : IconNames.CHEVRON_DOWN} aria-hidden />
          </button>
        </div>;
      })}
    </div>
    {expandedGroup && <div
      id={`${pipelineId}-pipeline-group-${expandedGroup.id}`}
      className="pipeline-group-details"
      role="region"
      aria-label={`${expandedGroup.label} detailed stages`}
    >
      <header><strong>{expandedGroup.label}</strong><span>{expandedGroup.stages.length} detailed stages</span></header>
      {expandedGroup.stages.length > 0
        ? <div className="pipeline-detail-list" role="list">{expandedGroup.stages.map((stage) => <DetailedStage key={stage.id} stage={stage} />)}</div>
        : <p className="pipeline-group-empty">No detailed stages recorded.</p>}
    </div>}
  </section>;
}

export function CurrentStage({
  currentStage,
  stages,
}: {
  currentStage: string | null | undefined;
  stages?: PipelineStage[];
}) {
  if (!currentStage) return null;
  const label = stages?.find((stage) => stage.id === currentStage)?.label
    ?? currentStage.replaceAll("_", " ");
  return <span className="current-stage">{label}</span>;
}
