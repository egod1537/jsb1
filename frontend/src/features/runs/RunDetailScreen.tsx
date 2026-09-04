import { Button, ButtonGroup, Tooltip } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { Link, useParams } from "react-router-dom";
import { useCallback, useEffect, useMemo, useState } from "react";
import { api } from "../../api/client";
import { ErrorPanel, Loading } from "../../components/Loading";
import { PageHeader } from "../../components/PageHeader";
import { StatusTag } from "../../components/StatusTag";
import type { ControllerParameterDefinition } from "../../types/api";
import { BuildDetailsDialog, useBuildDetails } from "../builds";
import { ControllerParametersDialog } from "../parameters";
import { ExecutionPipeline, RUN_PIPELINE_GROUPS } from "../pipeline";
import { createRunVariantDataSource, RunAnalysisView, type IntraRunView } from "../plots";
import { ScenarioViewerDialog } from "../scenarios";
import { ArtifactsDialog } from "./ArtifactsDialog";
import { RollHoldAnalyzerPanel } from "./RollHoldAnalyzerPanel";
import { useRun } from "./useRunData";

const metricLabels: Record<string, string> = {
  settling_time_sec: "Settling time",
  overshoot_deg: "Overshoot",
  rms_error_deg: "RMS error",
  steady_state_error_deg: "Steady error",
  max_abs_aileron_deg: "Max aileron",
};
const metricDefinitions: Record<string, string> = {
  settling_time_sec: "Time after command onset until tracking error remains within ±0.5 deg.",
  overshoot_deg: "Maximum excursion beyond commanded roll in the command direction.",
  rms_error_deg: "RMS roll tracking error after command onset.",
  steady_state_error_deg: "Absolute mean tracking error over the last 20% of samples.",
  max_abs_aileron_deg: "Maximum absolute aileron deflection.",
};

function value(number: number | null, unit: string) {
  return number == null ? "Not settled" : `${number.toFixed(3)} ${unit}`;
}

function SummaryViewAction({
  disabled = false,
  label,
  onClick,
}: {
  disabled?: boolean;
  label: string;
  onClick: () => void;
}) {
  return <span className="run-summary-view-action">
    <Tooltip content={label} hoverOpenDelay={150}>
      <Button
        aria-label={label}
        disabled={disabled}
        icon={IconNames.EYE_OPEN}
        minimal
        onClick={onClick}
        small
      />
    </Tooltip>
  </span>;
}

export function RunDetailScreen() {
  const id = Number(useParams().id);
  const detail = useRun(id);
  const [scenarioViewerOpen, setScenarioViewerOpen] = useState(false);
  const [artifactsOpen, setArtifactsOpen] = useState(false);
  const [buildDetailsOpen, setBuildDetailsOpen] = useState(false);
  const [parametersOpen, setParametersOpen] = useState(false);
  const [parameterDefinitions, setParameterDefinitions] = useState<ControllerParameterDefinition[]>([]);
  const [view, setView] = useState<IntraRunView>("overlay");
  const loadScenarioSnapshot = useCallback(() => api.runScenario(id), [id]);
  const completed = detail.data?.run.status === "completed";
  const buildDetails = useBuildDetails(detail.data?.run.build_id ?? null);
  useEffect(() => {
    if (!parametersOpen || !detail.data?.run.branch) return;
    let active = true;
    api.runtimeParameters(detail.data.run.branch, detail.data.run.commit_sha)
      .then((catalog) => { if (active) setParameterDefinitions(catalog.parameters); })
      .catch(() => { if (active) setParameterDefinitions([]); });
    return () => { active = false; };
  }, [detail.data?.run.branch, detail.data?.run.commit_sha, parametersOpen]);
  const runVariants = detail.data
    ? detail.data.run.variants?.length
      ? detail.data.run.variants
      : [detail.data.run.execution_variant]
    : [];
  useEffect(() => {
    if (runVariants.length > 1) setView("overlay");
    else if (runVariants[0]) setView(runVariants[0]);
  }, [id, runVariants.join(",")]);
  const plotDataSource = useMemo(
    () => createRunVariantDataSource(id, runVariants, view),
    [id, runVariants.join(","), view],
  );
  if (!Number.isInteger(id)) return <main><ErrorPanel message="Invalid run id" /></main>;
  if (detail.loading) return <main><Loading label="Loading run" /></main>;
  if (detail.error || !detail.data) return <main><ErrorPanel message={detail.error ?? "Run not found"} /></main>;
  const { run, metrics, artifacts } = detail.data;
  const buildReused = run.stages.some((stage) => stage.id === "resolve_build" && stage.message?.toLowerCase().includes("reused"))
    || buildDetails.data?.reused === true;
  const metricsPanel = metrics.length > 0 && <section className="panel metric-panel">
    <header className="panel-header"><span className="panel-title">Response metrics</span></header>
    <dl className="metric-list">
      {metrics.map((metric) => <div key={metric.name} title={metricDefinitions[metric.name]}>
        <dt>{metricLabels[metric.name] ?? metric.name}</dt>
        <dd>{value(metric.value, metric.unit)}</dd>
      </div>)}
    </dl>
  </section>;
  return (
    <main className="analysis-detail-page">
      <Link className="back-link" to="/runs">← All runs</Link>
      <PageHeader eyebrow={run.scenario_name} title={`Run #${run.id}`} actions={<>
        <StatusTag status={run.status} />
        <Button
          aria-label={`Open artifacts (${artifacts.length})`}
          disabled={artifacts.length === 0}
          icon={IconNames.FOLDER_SHARED_OPEN}
          onClick={() => setArtifactsOpen(true)}
          small
        >Artifacts {artifacts.length}</Button>
      </>} />
      <section className="property-grid run-summary-panel" aria-label="Run summary">
        <div><span>Scenario</span><div className="run-summary-value"><strong>{run.scenario_name}</strong><SummaryViewAction label="View Scenario Snapshot" onClick={() => setScenarioViewerOpen(true)} /></div></div>
        <div><span>Runtime</span><strong className="technical-value">{run.branch ?? run.build_branch ?? "detached"} @ {run.commit_sha?.slice(0, 10) ?? "—"}</strong><small>{run.repository_name ?? "Legacy runner"}</small></div>
        <div><span>Build</span><div className="run-build-summary">
          <strong className="technical-value">{run.build_id ? `#${run.build_id}` : "—"}</strong>
          {buildDetails.data && <StatusTag status={buildDetails.data.status} />}
          {buildReused && <small>REUSED</small>}
          {run.build_id && <SummaryViewAction label="View Build" onClick={() => setBuildDetailsOpen(true)} />}
        </div></div>
        <div><span>Variants</span><strong className="technical-value">{runVariants.join(" + ")}</strong></div>
        <div><span>Controller Parameters</span><div className="run-summary-value"><strong className="technical-value">{Object.keys(run.controller_parameters ?? {}).length || "—"}</strong><SummaryViewAction disabled={Object.keys(run.controller_parameters ?? {}).length === 0} label="View Controller Parameters" onClick={() => setParametersOpen(true)} /></div></div>
        <div><span>Simulation</span><strong>{run.simulation_time_sec == null ? "—" : `${run.simulation_time_sec.toFixed(2)} s`}</strong></div>
        <div><span>Wall time</span><strong>{run.wall_time_sec == null ? "—" : `${run.wall_time_sec.toFixed(2)} s`}</strong></div>
      </section>
      <ExecutionPipeline stages={run.stages ?? []} groups={RUN_PIPELINE_GROUPS} title="Run pipeline" />
      {run.error_message && <ErrorPanel message={run.error_message} />}
      {(run.status === "queued" || run.status === "running") && <Loading label={run.status === "queued" ? "Resolving build and waiting for a worker" : "Simulation running"} />}
      {completed ? <RunAnalysisView
        dataSource={plotDataSource}
        scenarioType={run.scenario_type}
        heading={<div className="section-heading workspace-heading">
          <div><span className="eyebrow">Recorded telemetry</span><h2>Analysis workspace</h2></div>
          <div className="intra-run-view" aria-label="Telemetry view">
            <span>View</span>
            <ButtonGroup minimal>
              {runVariants.map((variant) => <Button active={view === variant} key={variant} onClick={() => setView(variant)} small>{variant.replaceAll("_", " ").replace(/\b\w/g, (letter) => letter.toUpperCase())}</Button>)}
              <Button active={view === "overlay"} disabled={runVariants.length < 2} onClick={() => setView("overlay")} small>Overlay</Button>
            </ButtonGroup>
          </div>
        </div>}
        inspector={({ focusTimeRange, timeline, onVisibleRangeChange, onCursorTimeChange }) => <aside className="telemetry-inspector" aria-label="Run inspector">
          {run.scenario_type === "roll_hold" && <RollHoldAnalyzerPanel
            runId={run.id}
            timeline={timeline}
            onVisibleRangeChange={onVisibleRangeChange}
            onCursorTimeChange={onCursorTimeChange}
            onFocusRange={focusTimeRange}
            onViewParameters={() => setParametersOpen(true)}
          />}
          {metricsPanel}
        </aside>}
      /> : <div className="detail-side-panels">{metricsPanel}</div>}
      <ArtifactsDialog artifacts={artifacts} isOpen={artifactsOpen} onClose={() => setArtifactsOpen(false)} runId={run.id} />
      <BuildDetailsDialog
        build={buildDetails.data}
        buildId={run.build_id}
        error={buildDetails.error}
        isOpen={buildDetailsOpen}
        loading={buildDetails.loading}
        onClose={() => setBuildDetailsOpen(false)}
        reused={buildReused}
      />
      <ScenarioViewerDialog isOpen={scenarioViewerOpen} load={loadScenarioSnapshot} onClose={() => setScenarioViewerOpen(false)} title={`${run.scenario_name} · Executed snapshot`} />
      <ControllerParametersDialog
        definitions={parameterDefinitions}
        isOpen={parametersOpen}
        onClose={() => setParametersOpen(false)}
        parameters={run.controller_parameters ?? {}}
        title={`Run #${run.id} · Controller Parameters`}
      />
    </main>
  );
}
