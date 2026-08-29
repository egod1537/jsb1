import { Button, HTMLTable, Intent } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { useState } from "react";
import { Link, useSearchParams } from "react-router-dom";
import { ErrorPanel, Loading } from "../components/Loading";
import { NewRunForm } from "../components/NewRunForm";
import { PageHeader } from "../components/PageHeader";
import { StatusTag } from "../components/StatusTag";
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
      <PageHeader
        eyebrow="Regression workspace"
        title="Simulation runs"
        description="Headless execution history, artifacts, and flight-control metrics."
        actions={<Button icon={IconNames.ADD} intent={Intent.PRIMARY} onClick={() => setCreating(true)}>New run</Button>}
      />
      {loading && <Loading label="Loading runs" />}
      {error && <ErrorPanel message={error} />}
      {data && (
        <div className="table-shell">
          <HTMLTable compact interactive striped>
            <thead><tr>
              <th>ID</th><th>Status</th><th>Scenario</th><th>Repository</th><th>Build</th><th>Commit</th>
              <th>Autopilot</th><th>Created</th><th>Duration</th>
            </tr></thead>
            <tbody>
              {data.map((run) => (
                <tr key={run.id}>
                  <td><Link className="run-id" to={`/runs/${run.id}`}>#{run.id}</Link></td>
                  <td><StatusTag status={run.status} /></td>
                  <td><Link to={`/runs/${run.id}`}>{run.scenario_name}</Link></td>
                  <td>{run.repository_name ?? "Legacy"}</td>
                  <td className="technical-value">{run.build_id ? <Link to={`/builds?selected=${run.build_id}`}>#{run.build_id}</Link> : "—"}</td>
                  <td><code title={run.commit_sha ?? undefined}>{run.commit_sha?.slice(0, 10) ?? "—"}</code></td>
                  <td className="technical-value">{run.autopilot}</td><td className="technical-value">{time(run.created_at)}</td>
                  <td className="technical-value">{run.wall_time_sec == null ? "—" : `${run.wall_time_sec.toFixed(2)} s`}</td>
                </tr>
              ))}
              {data.length === 0 && <tr><td colSpan={9} className="empty">No runs yet. Queue the first one.</td></tr>}
            </tbody>
          </HTMLTable>
        </div>
      )}
      {showForm && <NewRunForm initialBuildId={requestedBuild} onClose={() => { setCreating(false); setSearchParams({}); }} />}
    </main>
  );
}
