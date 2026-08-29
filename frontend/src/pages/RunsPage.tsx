import { useState } from "react";
import { Link, useSearchParams } from "react-router-dom";
import { ErrorPanel, Loading } from "../components/Loading";
import { NewRunForm } from "../components/NewRunForm";
import { StatusBadge } from "../components/StatusBadge";
import { useRuns } from "../features/runs/useRunData";

function time(value: string) {
  return new Intl.DateTimeFormat(undefined, {
    month: "short", day: "2-digit", hour: "2-digit", minute: "2-digit",
  }).format(new Date(value));
}

export function RunsPage() {
  const { data, loading, error } = useRuns();
  const [creating, setCreating] = useState(false);
  const [searchParams, setSearchParams] = useSearchParams();
  const requestedBuild = Number(searchParams.get("build_id")) || undefined;
  const showForm = creating || requestedBuild !== undefined;
  return (
    <main>
      <div className="page-heading">
        <div>
          <span className="eyebrow">Regression workspace</span>
          <h1>Simulation runs</h1>
          <p>Headless execution history, artifacts, and flight-control metrics.</p>
        </div>
        <button className="button" onClick={() => setCreating(true)}>＋ New run</button>
      </div>
      {loading && <Loading label="Loading runs" />}
      {error && <ErrorPanel message={error} />}
      {data && (
        <div className="table-shell">
          <table>
            <thead><tr>
              <th>ID</th><th>Status</th><th>Scenario</th><th>Repository</th><th>Build</th><th>Commit</th>
              <th>Autopilot</th><th>Created</th><th>Duration</th>
            </tr></thead>
            <tbody>
              {data.map((run) => (
                <tr key={run.id}>
                  <td><Link className="run-id" to={`/runs/${run.id}`}>#{run.id}</Link></td>
                  <td><StatusBadge status={run.status} /></td>
                  <td><Link to={`/runs/${run.id}`}>{run.scenario_name}</Link></td>
                  <td>{run.repository_name ?? "Legacy"}</td>
                  <td>{run.build_id ? <Link to={`/builds?selected=${run.build_id}`}>#{run.build_id}</Link> : "—"}</td>
                  <td><code title={run.commit_sha ?? undefined}>{run.commit_sha?.slice(0, 10) ?? "—"}</code></td>
                  <td>{run.autopilot}</td><td>{time(run.created_at)}</td>
                  <td>{run.wall_time_sec == null ? "—" : `${run.wall_time_sec.toFixed(2)} s`}</td>
                </tr>
              ))}
              {data.length === 0 && <tr><td colSpan={9} className="empty">No runs yet. Queue the first one.</td></tr>}
            </tbody>
          </table>
        </div>
      )}
      {showForm && <NewRunForm initialBuildId={requestedBuild} onClose={() => { setCreating(false); setSearchParams({}); }} />}
    </main>
  );
}
