import { useEffect, useMemo, useState } from "react";
import { api } from "../api/client";
import { ErrorPanel, Loading } from "../components/Loading";
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
    <div className="page-heading"><div><span className="eyebrow">A/B inspection</span><h1>Compare runs</h1><p>Overlay matching scenarios and inspect metric deltas.</p></div></div>
    {runs.loading && <Loading label="Loading completed runs" />}
    {runs.error && <ErrorPanel message={runs.error} />}
    {runs.data && <section className="compare-picker">
      <label>Run A<select value={a ?? ""} onChange={(event) => { setA(Number(event.target.value) || null); setB(null); }}><option value="">Select a run</option>{completed.map((run) => <option key={run.id} value={run.id}>#{run.id} · {run.scenario_name} · {run.commit_sha.slice(0, 7)}</option>)}</select></label>
      <span>versus</span>
      <label>Run B<select value={b ?? ""} disabled={!a} onChange={(event) => setB(Number(event.target.value) || null)}><option value="">Select a matching run</option>{candidatesB.map((run) => <option key={run.id} value={run.id}>#{run.id} · {run.commit_sha.slice(0, 7)}</option>)}</select></label>
    </section>}
    {error && <ErrorPanel message={error} />}
    {a && b && !details && !error && <Loading label="Building comparison" />}
    {details && <div className="table-shell compare-table"><table><thead><tr><th>Metric</th><th>Run #{details[0].run.id}</th><th>Run #{details[1].run.id}</th><th>Delta (B − A)</th></tr></thead><tbody>{metricRows.map((row) => {
      const av = row.a?.value; const bv = row.b?.value; const delta = av != null && bv != null ? bv - av : null; const unit = row.a?.unit ?? row.b?.unit ?? "";
      return <tr key={row.name}><td>{row.name}</td><td>{av == null ? "—" : `${av.toFixed(3)} ${unit}`}</td><td>{bv == null ? "—" : `${bv.toFixed(3)} ${unit}`}</td><td className={delta == null ? "" : delta > 0 ? "delta-up" : "delta-down"}>{delta == null ? "—" : `${delta > 0 ? "+" : ""}${delta.toFixed(3)} ${unit}`}</td></tr>;
    })}</tbody></table></div>}
    {details && signals && <section className="plots"><TimeSeriesChart title="Roll tracking overlay" unit="deg" series={[
      { name: "Commanded", time: signals[0].time, values: signals[0].series.commanded_roll, color: "#f2bd52", dashed: true },
      { name: `Run #${details[0].run.id}`, time: signals[0].time, values: signals[0].series.roll, color: "#21c8b5" },
      { name: `Run #${details[1].run.id}`, time: signals[1].time, values: signals[1].series.roll, color: "#5b8def" },
    ]} /></section>}
  </main>;
}

