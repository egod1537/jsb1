import { Button, HTMLTable } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { useCallback, useEffect, useState } from "react";
import { Link, useSearchParams } from "react-router-dom";
import { api } from "../api/client";
import { ErrorPanel, Loading } from "../components/Loading";
import { PageHeader } from "../components/PageHeader";
import { StatusTag } from "../components/StatusTag";
import { showSuccess } from "../components/toast";
import type { Build } from "../types/api";

function duration(build: Build) {
  if (!build.started_at || !build.completed_at) return "—";
  return `${((new Date(build.completed_at).getTime() - new Date(build.started_at).getTime()) / 1000).toFixed(1)} s`;
}

export function BuildsPage() {
  const [items, setItems] = useState<Build[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState<number | null>(null);
  const [params] = useSearchParams();
  const selected = Number(params.get("selected")) || null;
  const load = useCallback(() => api.builds().then(setItems).catch((reason: Error) => setError(reason.message)), []);
  useEffect(() => {
    load();
    const timer = window.setInterval(load, 3000);
    return () => window.clearInterval(timer);
  }, [load]);

  async function rebuild(id: number) {
    setBusy(id);
    setError(null);
    try {
      await api.rebuild(id);
      await load();
      void showSuccess(`Build #${id} queued`);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Rebuild failed");
    } finally {
      setBusy(null);
    }
  }

  return <main>
    <PageHeader eyebrow="Revision artifacts" title="Builds" description="Immutable CMake outputs linked to exact repository commits." />
    {!items && !error && <Loading label="Loading builds" />}
    {error && <ErrorPanel message={error} />}
    {items && <div className="table-shell"><HTMLTable compact interactive striped><thead><tr><th>ID</th><th>Repository</th><th>Revision</th><th>Commit</th><th>Status</th><th>Created</th><th>Duration</th><th>Actions</th></tr></thead><tbody>{items.map((item) => <tr key={item.id} className={selected === item.id ? "selected-row" : ""}><td className="run-id">#{item.id}</td><td><Link to={`/repositories/${item.repository_id}`}>{item.repository_name}</Link></td><td className="technical-value">{item.branch ?? "detached"}</td><td><code title={item.commit_sha}>{item.commit_sha.slice(0, 10)}</code></td><td><StatusTag status={item.status} /></td><td className="technical-value">{new Date(item.created_at).toLocaleString()}</td><td className="technical-value">{duration(item)}</td><td className="actions"><div className="action-group"><a href={`/api/builds/${item.id}/logs/stdout`} target="_blank" rel="noreferrer">Logs</a>{item.status === "completed" && item.branch && <Link to={`/runs?new=1&branch=${encodeURIComponent(item.branch)}`}>Run simulation</Link>}<Button icon={IconNames.REFRESH} loading={busy === item.id} minimal small onClick={() => rebuild(item.id)}>Rebuild</Button></div></td></tr>)}{items.length === 0 && <tr><td colSpan={8} className="empty">No builds yet. Select a repository revision first.</td></tr>}</tbody></HTMLTable></div>}
  </main>;
}
