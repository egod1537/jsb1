import { Link, useParams } from "react-router-dom";
import { ErrorPanel, Loading } from "../components/Loading";
import { StatusBadge } from "../components/StatusBadge";
import { TimeSeriesChart } from "../components/TimeSeriesChart";
import { useRun, useSignals } from "../features/runs/useRunData";

const channels = ["commanded_roll", "roll", "commanded_roll_rate", "roll_rate", "aileron"];
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

export function RunDetailPage() {
  const id = Number(useParams().id);
  const detail = useRun(id);
  const completed = detail.data?.run.status === "completed";
  const signals = useSignals(id, completed, channels);
  if (!Number.isInteger(id)) return <main><ErrorPanel message="Invalid run id" /></main>;
  if (detail.loading) return <main><Loading label="Loading run" /></main>;
  if (detail.error || !detail.data) return <main><ErrorPanel message={detail.error ?? "Run not found"} /></main>;
  const { run, metrics, artifacts } = detail.data;
  const telemetry = signals.data;
  return (
    <main>
      <Link className="back-link" to="/runs">← All runs</Link>
      <div className="page-heading page-heading--detail">
        <div><span className="eyebrow">{run.scenario_name}</span><h1>Run #{run.id}</h1></div>
        <StatusBadge status={run.status} />
      </div>
      <section className="run-facts run-facts--lineage">
        <div><span>Repository</span><strong>{run.repository_name ?? "Legacy runner"}</strong></div>
        <div><span>Branch</span><strong>{run.build_branch ?? "—"}</strong></div>
        <div><span>Commit</span><code title={run.commit_sha ?? undefined}>{run.commit_sha?.slice(0, 10) ?? "—"}</code></div>
        <div><span>Build</span><strong>{run.build_id ? `#${run.build_id}` : "—"}</strong></div>
        <div><span>Autopilot</span><strong>{run.autopilot}</strong></div>
        <div><span>Simulation</span><strong>{run.simulation_time_sec == null ? "—" : `${run.simulation_time_sec.toFixed(2)} s`}</strong></div>
        <div><span>Wall time</span><strong>{run.wall_time_sec == null ? "—" : `${run.wall_time_sec.toFixed(2)} s`}</strong></div>
      </section>
      {run.error_message && <ErrorPanel message={run.error_message} />}
      {(run.status === "queued" || run.status === "running") && <Loading label={run.status === "queued" ? "Waiting for a worker" : "Simulation running"} />}
      {metrics.length > 0 && <section><div className="metric-grid">
        {metrics.map((metric) => <article className="metric-card" key={metric.name} title={metricDefinitions[metric.name]}>
          <span>{metricLabels[metric.name] ?? metric.name}</span>
          <strong>{value(metric.value, metric.unit)}</strong>
        </article>)}
      </div></section>}
      {completed && signals.loading && <Loading label="Loading telemetry" />}
      {signals.error && <ErrorPanel message={signals.error} />}
      {telemetry && <section className="plots">
        <div className="section-heading"><div><span className="eyebrow">Recorded telemetry</span><h2>Flight response</h2></div><small>{telemetry.returned_points.toLocaleString()} / {telemetry.source_points.toLocaleString()} points</small></div>
        <TimeSeriesChart title="Roll tracking" unit="deg" group={`run-${id}`} series={[
          { name: "Commanded", time: telemetry.time, values: telemetry.series.commanded_roll, color: "#f2bd52", dashed: true },
          { name: "Actual", time: telemetry.time, values: telemetry.series.roll, color: "#21c8b5" },
        ]} />
        <TimeSeriesChart title="Roll rate" unit="deg/s" group={`run-${id}`} series={[
          { name: "Commanded", time: telemetry.time, values: telemetry.series.commanded_roll_rate, color: "#f2bd52", dashed: true },
          { name: "Actual", time: telemetry.time, values: telemetry.series.roll_rate, color: "#5b8def" },
        ]} />
        <TimeSeriesChart title="Aileron" unit="deg" group={`run-${id}`} series={[
          { name: "Aileron", time: telemetry.time, values: telemetry.series.aileron, color: "#d56ef4" },
        ]} />
      </section>}
      {artifacts.length > 0 && <section className="artifacts"><h2>Artifacts</h2><div>
        {artifacts.map((artifact) => <a key={artifact.id} href={artifact.download_url}>{artifact.filename}<span>{artifact.kind}</span></a>)}
      </div></section>}
    </main>
  );
}
