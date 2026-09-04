import { Callout, HTMLTable, Intent, Tag } from "@blueprintjs/core";
import { useEffect, useMemo, useState } from "react";
import { Link, useSearchParams } from "react-router-dom";
import { api } from "../../api/client";
import { ErrorPanel, Loading } from "../../components/Loading";
import { PageHeader } from "../../components/PageHeader";
import { StatusTag } from "../../components/StatusTag";
import type { Metric, RollHoldAnalysis, RollHoldAnalysisVariants, RunDetail } from "../../types/api";
import { createRunComparisonDataSource, RunAnalysisView } from "../plots";

function parseRunId(value: string | null): number | null {
  if (value == null || !/^\d+$/.test(value)) return null;
  const id = Number(value);
  return Number.isSafeInteger(id) && id > 0 ? id : null;
}

function shortCommit(commit: string | null): string {
  return commit?.slice(0, 10) ?? "—";
}

function runLabel(prefix: "A" | "B", detail: RunDetail): string {
  return `${prefix} — Run #${detail.run.id} · ${shortCommit(detail.run.commit_sha)}`;
}

function metricLabel(name: string): string {
  return name.replaceAll("_", " ").replace(/\b\w/g, (letter) => letter.toUpperCase());
}

function metricValue(value: number | null, unit: string): string {
  return value == null ? "—" : `${value.toFixed(3)}${unit ? ` ${unit}` : ""}`;
}

function MetricsComparison({ a, b, labelA, labelB }: {
  a: Metric[];
  b: Metric[];
  labelA: string;
  labelB: string;
}) {
  const names = [...new Set([...a.map((metric) => metric.name), ...b.map((metric) => metric.name)])];
  if (names.length === 0) return <div className="empty-table-state">No comparable metrics are available.</div>;
  return <div className="table-shell compare-table"><HTMLTable compact>
    <thead><tr><th>Metric</th><th>{labelA}</th><th>{labelB}</th><th>Δ B − A</th></tr></thead>
    <tbody>{names.map((name) => {
      const left = a.find((metric) => metric.name === name) ?? null;
      const right = b.find((metric) => metric.name === name) ?? null;
      const unit = left?.unit || right?.unit || "";
      const delta = left?.value != null && right?.value != null ? right.value - left.value : null;
      return <tr key={name}>
        <td>{metricLabel(name)}</td>
        <td className="technical-value">{metricValue(left?.value ?? null, left?.unit ?? unit)}</td>
        <td className="technical-value">{metricValue(right?.value ?? null, right?.unit ?? unit)}</td>
        <td className="technical-value">{delta == null ? "—" : `${delta >= 0 ? "+" : ""}${delta.toFixed(3)}${unit ? ` ${unit}` : ""}`}</td>
      </tr>;
    })}</tbody>
  </HTMLTable></div>;
}

function analyzerMetrics(result: RollHoldAnalysis): Metric[] {
  return Object.entries(result.metrics).flatMap(([name, value]) => typeof value === "number"
    ? [{ name, value, unit: result.metric_units[name] ?? "" }]
    : []);
}

function preferredAnalysis(result: RollHoldAnalysis | RollHoldAnalysisVariants): RollHoldAnalysis {
  if (!("variants" in result)) return result;
  return Object.values(result.variants).at(-1)!;
}

function ParameterComparison({ a, b, labelA, labelB }: {
  a: Record<string, number>;
  b: Record<string, number>;
  labelA: string;
  labelB: string;
}) {
  const ids = [...new Set([...Object.keys(a), ...Object.keys(b)])].sort();
  if (ids.length === 0) return <div className="empty-table-state">Neither Run recorded controller parameters.</div>;
  return <div className="table-shell compare-table"><HTMLTable compact>
    <thead><tr><th>Parameter</th><th>{labelA}</th><th>{labelB}</th><th>Δ B − A</th></tr></thead>
    <tbody>{ids.map((id) => {
      const left = a[id];
      const right = b[id];
      const delta = left != null && right != null ? right - left : null;
      return <tr key={id}>
        <td><code>{id}</code></td>
        <td className="technical-value">{left ?? "—"}</td>
        <td className="technical-value">{right ?? "—"}</td>
        <td className="technical-value">{delta == null ? "—" : `${delta >= 0 ? "+" : ""}${delta}`}</td>
      </tr>;
    })}</tbody>
  </HTMLTable></div>;
}

export function RunCompareScreen() {
  const [params] = useSearchParams();
  const runAId = parseRunId(params.get("a"));
  const runBId = parseRunId(params.get("b"));
  const [details, setDetails] = useState<[RunDetail, RunDetail] | null>(null);
  const [analysis, setAnalysis] = useState<[RollHoldAnalysis, RollHoldAnalysis] | null>(null);
  const [analysisError, setAnalysisError] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [view, setView] = useState<"overlay" | "side-by-side">("overlay");

  useEffect(() => {
    if (runAId == null || runBId == null || runAId === runBId) return;
    let active = true;
    setDetails(null);
    setAnalysis(null);
    setAnalysisError(null);
    setError(null);
    void Promise.all([api.run(runAId), api.run(runBId)])
      .then(async (loaded) => {
        if (!active) return;
        setDetails(loaded);
        if (loaded.every((detail) => detail.run.scenario_type === "roll_hold")) {
          try {
            const results = await Promise.all([api.rollHoldAnalysis(runAId), api.rollHoldAnalysis(runBId)]);
            if (active) setAnalysis([preferredAnalysis(results[0]), preferredAnalysis(results[1])]);
          } catch (reason) {
            if (active) setAnalysisError(reason instanceof Error ? reason.message : "Could not compare Roll Hold analysis");
          }
        }
      })
      .catch((reason: unknown) => {
        if (active) setError(reason instanceof Error ? reason.message : "Could not load runs for comparison");
      });
    return () => { active = false; };
  }, [runAId, runBId]);

  const labels = useMemo(() => details ? [runLabel("A", details[0]), runLabel("B", details[1])] as const : null, [details]);
  const completed = details?.every((detail) => detail.run.status === "completed") ?? false;
  const dataSource = useMemo(() => details && labels && completed ? createRunComparisonDataSource([
    { runId: details[0].run.id, label: labels[0], variant: details[0].run.variants?.at(-1) ?? details[0].run.execution_variant },
    { runId: details[1].run.id, label: labels[1], variant: details[1].run.variants?.at(-1) ?? details[1].run.execution_variant },
  ]) : null, [completed, details, labels]);

  if (runAId == null || runBId == null || runAId === runBId) {
    return <main><ErrorPanel message="Select two different Runs from the Runs table to compare." /></main>;
  }
  if (error) return <main><ErrorPanel message={error} /></main>;
  if (!details || !labels) return <main><Loading label="Loading Runs for comparison" /></main>;

  const [runA, runB] = details;
  const scenarioA = runA.run.scenario_sha256 ?? runA.run.scenario_id ?? runA.run.scenario_name;
  const scenarioB = runB.run.scenario_sha256 ?? runB.run.scenario_id ?? runB.run.scenario_name;
  const scenarioDiffers = scenarioA !== scenarioB;
  const commitDiffers = runA.run.commit_sha !== runB.run.commit_sha;
  const scenarioType = runA.run.scenario_type ?? runB.run.scenario_type;

  return <main className="analysis-detail-page run-compare-page">
    <Link className="back-link" to="/runs">← All runs</Link>
    <PageHeader title="Compare runs" description={`${labels[0]}  vs  ${labels[1]}`} />

    <section className="compare-run-overview" aria-label="Compared runs">
      {[runA, runB].map((detail, index) => <article className="panel compare-run-card" key={detail.run.id}>
        <header className="panel-header"><span>{index === 0 ? "Run A" : "Run B"}</span><StatusTag status={detail.run.status} /></header>
        <div className="panel-body">
          <h2><Link to={`/runs/${detail.run.id}`}>Run #{detail.run.id}</Link></h2>
          <dl>
            <div><dt>Scenario</dt><dd>{detail.run.scenario_name}</dd></div>
            <div><dt>Runtime</dt><dd>{detail.run.branch ?? detail.run.build_branch ?? "detached"} @ <code>{shortCommit(detail.run.commit_sha)}</code></dd></div>
            <div><dt>Build</dt><dd>{detail.run.build_id ? `#${detail.run.build_id}` : "—"}</dd></div>
            <div><dt>Variants</dt><dd>{detail.run.variants?.join(" + ") || detail.run.execution_variant}</dd></div>
          </dl>
        </div>
      </article>)}
    </section>

    {(scenarioDiffers || commitDiffers) && <div className="compare-warning-list">
      {scenarioDiffers && <Callout compact intent={Intent.WARNING}>Scenario differs. Telemetry remains aligned by simulation time.</Callout>}
      {commitDiffers && <Callout compact intent={Intent.WARNING}>Runtime commit differs. Results represent different immutable JSB0 revisions.</Callout>}
    </div>}

    <section className="panel compare-differences" aria-label="Run differences">
      <header className="panel-header"><span className="panel-title">Differences</span></header>
      <div className="table-shell"><HTMLTable compact>
        <thead><tr><th>Property</th><th>Run A</th><th>Run B</th><th>Match</th></tr></thead>
        <tbody>
          {[
            ["Scenario", runA.run.scenario_name, runB.run.scenario_name, !scenarioDiffers],
            ["Scenario SHA-256", runA.run.scenario_sha256 ?? "—", runB.run.scenario_sha256 ?? "—", !scenarioDiffers],
            ["Runtime branch", runA.run.branch ?? runA.run.build_branch ?? "—", runB.run.branch ?? runB.run.build_branch ?? "—", (runA.run.branch ?? runA.run.build_branch) === (runB.run.branch ?? runB.run.build_branch)],
            ["Runtime commit", runA.run.commit_sha ?? "—", runB.run.commit_sha ?? "—", !commitDiffers],
            ["Execution variants", runA.run.variants?.join(" + ") || runA.run.execution_variant, runB.run.variants?.join(" + ") || runB.run.execution_variant, JSON.stringify(runA.run.variants) === JSON.stringify(runB.run.variants)],
          ].map(([name, left, right, matches]) => <tr key={String(name)}>
            <td>{String(name)}</td><td><code>{String(left)}</code></td><td><code>{String(right)}</code></td>
            <td><Tag intent={matches ? Intent.SUCCESS : Intent.WARNING} minimal>{matches ? "SAME" : "DIFF"}</Tag></td>
          </tr>)}
        </tbody>
      </HTMLTable></div>
    </section>

    <section className="panel compare-metrics" aria-label="Metrics comparison">
      <header className="panel-header"><span className="panel-title">Metrics</span></header>
      <MetricsComparison a={runA.metrics} b={runB.metrics} labelA={`Run #${runA.run.id}`} labelB={`Run #${runB.run.id}`} />
    </section>

    <section className="panel compare-parameters" aria-label="Controller parameter comparison">
      <header className="panel-header"><span className="panel-title">Controller Parameters</span></header>
      <ParameterComparison
        a={runA.run.controller_parameters ?? {}}
        b={runB.run.controller_parameters ?? {}}
        labelA={`Run #${runA.run.id}`}
        labelB={`Run #${runB.run.id}`}
      />
    </section>

    {dataSource ? <RunAnalysisView
      dataSource={dataSource}
      scenarioType={scenarioType}
      initialLayout="2x2"
      comparisonView={view}
      onComparisonViewChange={setView}
      heading={<div className="section-heading workspace-heading"><div><span className="eyebrow">Simulation-time aligned</span><h2>Telemetry</h2></div></div>}
    /> : <Callout compact intent={Intent.WARNING}>Both Runs must be completed before telemetry can be overlaid.</Callout>}

    {runA.run.scenario_type === "roll_hold" && runB.run.scenario_type === "roll_hold" && <section className="panel compare-analyzer" aria-label="Roll Hold analyzer comparison">
      <header className="panel-header"><span className="panel-title">Roll Hold Analyzer</span></header>
      {analysis
        ? <MetricsComparison a={analyzerMetrics(analysis[0])} b={analyzerMetrics(analysis[1])} labelA={`Run #${runA.run.id}`} labelB={`Run #${runB.run.id}`} />
        : analysisError ? <Callout compact intent={Intent.WARNING}>{analysisError}</Callout>
        : <Loading label="Loading Roll Hold analysis" />}
    </section>}
  </main>;
}
