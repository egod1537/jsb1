import { FormGroup, HTMLSelect, HTMLTable } from "@blueprintjs/core";
import { useEffect, useMemo, useState } from "react";
import { api } from "../api/client";
import { ErrorPanel, Loading } from "../components/Loading";
import { PageHeader } from "../components/PageHeader";
import { TimeSeriesChart } from "../components/TimeSeriesChart";
import { useRuns } from "../features/runs/useRunData";
import type { RunDetail, SignalResponse } from "../types/api";

const compareChannels = ["commanded_roll", "roll"];

export function ComparePage() {
  const runs = useRuns(false);
  const completed = useMemo(() => runs.data?.filter((run) => run.status === "completed") ?? [], [runs.data]);
  const [a, setA] = useState<number | null>(null);
  const [b, setB] = useState<number | null>(null);
  const [details, setDetails] = useState<[RunDetail, RunDetail] | null>(null);
  const [signals, setSignals] = useState<[SignalResponse, SignalResponse] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const selectedA = completed.find((run) => run.id === a);
  const candidatesB = selectedA ? completed.filter((run) => run.scenario_name === selectedA.scenario_name && run.id !== a) : [];

  useEffect(() => {
    if (!a || !b) { setDetails(null); setSignals(null); return; }
    setError(null);
    Promise.all([
      api.run(a), api.run(b), api.signals(a, compareChannels), api.signals(b, compareChannels),
    ]).then(([detailA, detailB, signalA, signalB]) => {
      setDetails([detailA, detailB]); setSignals([signalA, signalB]);
    }).catch((reason: Error) => setError(reason.message));
  }, [a, b]);

  const metricRows = useMemo(() => {
    if (!details) return [];
    const left = new Map(details[0].metrics.map((metric) => [metric.name, metric]));
    const right = new Map(details[1].metrics.map((metric) => [metric.name, metric]));
    return [...new Set([...left.keys(), ...right.keys()])].map((name) => ({ name, a: left.get(name), b: right.get(name) }));
  }, [details]);

  return <main>
    <PageHeader eyebrow="A/B inspection" title="Compare runs" description="Overlay matching scenarios and inspect metric deltas." />
    {runs.loading && <Loading label="Loading completed runs" />}
    {runs.error && <ErrorPanel message={runs.error} />}
    {runs.data && <section className="compare-picker" aria-label="Run selection">
      <FormGroup label="Run A" labelFor="compare-run-a"><HTMLSelect id="compare-run-a" fill value={a ?? ""} onChange={(event) => { setA(Number(event.currentTarget.value) || null); setB(null); }}><option value="">Select a run</option>{completed.map((run) => <option key={run.id} value={run.id}>#{run.id} · {run.scenario_name} · {run.branch ?? run.build_branch ?? "legacy"} @ {run.commit_sha?.slice(0, 10) ?? "unknown"} · {run.autopilot}</option>)}</HTMLSelect></FormGroup>
      <span>versus</span>
      <FormGroup label="Run B" labelFor="compare-run-b"><HTMLSelect id="compare-run-b" fill value={b ?? ""} disabled={!a} onChange={(event) => setB(Number(event.currentTarget.value) || null)}><option value="">Select a matching run</option>{candidatesB.map((run) => <option key={run.id} value={run.id}>#{run.id} · {run.branch ?? run.build_branch ?? "legacy"} @ {run.commit_sha?.slice(0, 10) ?? "unknown"} · {run.autopilot}</option>)}</HTMLSelect></FormGroup>
    </section>}
    {error && <ErrorPanel message={error} />}
    {a && b && !details && !error && <Loading label="Building comparison" />}
    {details && <div className="table-shell compare-lineage"><HTMLTable compact><thead><tr><th>Run lineage</th><th>Run #{details[0].run.id}</th><th>Run #{details[1].run.id}</th></tr></thead><tbody>
      <tr><td>Branch</td><td className="technical-value">{details[0].run.branch ?? details[0].run.build_branch ?? "legacy"}</td><td className="technical-value">{details[1].run.branch ?? details[1].run.build_branch ?? "legacy"}</td></tr>
      <tr><td>Commit</td><td><code title={details[0].run.commit_sha ?? undefined}>{details[0].run.commit_sha?.slice(0, 10) ?? "—"}</code></td><td><code title={details[1].run.commit_sha ?? undefined}>{details[1].run.commit_sha?.slice(0, 10) ?? "—"}</code></td></tr>
      <tr><td>Autopilot</td><td className="technical-value">{details[0].run.autopilot}</td><td className="technical-value">{details[1].run.autopilot}</td></tr>
      <tr><td>Scenario</td><td className="technical-value">{details[0].run.scenario_name}</td><td className="technical-value">{details[1].run.scenario_name}</td></tr>
    </tbody></HTMLTable></div>}
    {details && <div className="table-shell compare-table"><HTMLTable compact interactive striped><thead><tr><th>Metric</th><th>Run #{details[0].run.id}</th><th>Run #{details[1].run.id}</th><th>Delta (B − A)</th></tr></thead><tbody>{metricRows.map((row) => {
      const av = row.a?.value; const bv = row.b?.value; const delta = av != null && bv != null ? bv - av : null; const unit = row.a?.unit ?? row.b?.unit ?? "";
      return <tr key={row.name}><td>{row.name}</td><td className="technical-value">{av == null ? "—" : `${av.toFixed(3)} ${unit}`}</td><td className="technical-value">{bv == null ? "—" : `${bv.toFixed(3)} ${unit}`}</td><td className={`technical-value ${delta == null ? "" : delta > 0 ? "delta-up" : "delta-down"}`}>{delta == null ? "—" : `${delta > 0 ? "+" : ""}${delta.toFixed(3)} ${unit}`}</td></tr>;
    })}</tbody></HTMLTable></div>}
    {details && signals && <section className="plots"><TimeSeriesChart title="Roll tracking overlay" unit="deg" series={[
      { name: "Commanded", time: signals[0].time, values: signals[0].series.commanded_roll, color: "#f2bd52", dashed: true },
      { name: `Run #${details[0].run.id}`, time: signals[0].time, values: signals[0].series.roll, color: "#21c8b5" },
      { name: `Run #${details[1].run.id}`, time: signals[1].time, values: signals[1].series.roll, color: "#5b8def" },
    ]} /></section>}
  </main>;
}
