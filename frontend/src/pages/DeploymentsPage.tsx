import { FormEvent, useEffect, useMemo, useState } from "react";
import { api } from "../api/client";
import { ErrorPanel, Loading } from "../components/Loading";
import type { Branch, Deployment, Repository } from "../types/api";

export function DeploymentsPage() {
  const [items, setItems] = useState<Deployment[] | null>(null);
  const [repositories, setRepositories] = useState<Repository[]>([]);
  const [branches, setBranches] = useState<Branch[]>([]);
  const [repositoryId, setRepositoryId] = useState<number | null>(null);
  const [creating, setCreating] = useState(false);
  const [busy, setBusy] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  const load = async () => {
    try {
      setItems(await api.deployments());
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not load deployments");
    }
  };

  useEffect(() => {
    void load();
    void api.repositories().then((records) => {
      setRepositories(records);
      if (records.length > 0) setRepositoryId((current) => current ?? records[0].id);
    }).catch((reason: Error) => setError(reason.message));
    const timer = window.setInterval(() => { void load(); }, 3000);
    return () => window.clearInterval(timer);
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    if (repositoryId === null) {
      setBranches([]);
      return;
    }
    void api.branches(repositoryId)
      .then((records) => {
        const unique = new Map<string, Branch>();
        for (const branch of records) {
          const previous = unique.get(branch.name);
          if (!previous || (previous.remote && !branch.remote)) unique.set(branch.name, branch);
        }
        setBranches([...unique.values()].sort((a, b) => a.name.localeCompare(b.name)));
      })
      .catch((reason: Error) => setError(reason.message));
  }, [repositoryId]);

  const repositoryNames = useMemo(
    () => new Map(repositories.map((repository) => [repository.id, repository.name])),
    [repositories],
  );

  async function create(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    if (repositoryId === null) return;
    const values = new FormData(event.currentTarget);
    setBusy("create");
    setError(null);
    try {
      await api.createDeployment({ repository_id: repositoryId, branch: String(values.get("branch")) });
      setCreating(false);
      await load();
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Deployment could not be queued");
    } finally {
      setBusy(null);
    }
  }

  async function runAction(item: Deployment, action: "redeploy" | "restart" | "stop") {
    const key = `${action}-${item.id}`;
    if (action === "stop") {
      const warning = item.branch === "main"
        ? "Stop the main deployment? This explicitly sends force=true and takes the primary site offline."
        : `Stop ${item.branch}?`;
      if (!window.confirm(warning)) return;
    }
    setBusy(key);
    setError(null);
    try {
      if (action === "redeploy") await api.redeploy(item.id);
      if (action === "restart") await api.restartDeployment(item.id);
      if (action === "stop") await api.stopDeployment(item.id, item.branch === "main");
      await load();
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : `${action} failed`);
    } finally {
      setBusy(null);
    }
  }

  return <main>
    <div className="page-heading"><div><span className="eyebrow">Branch preview routing</span><h1>Deployments</h1><p>Immutable commit instances behind stable branch hostnames.</p></div><button className="button" onClick={() => setCreating(true)} disabled={repositories.length === 0}>＋ Deploy branch</button></div>
    {error && <ErrorPanel message={error} />}
    {!items && !error && <Loading label="Loading deployments" />}
    {items && <div className="table-shell"><table><thead><tr><th>Repository</th><th>Branch</th><th>Commit</th><th>Hostname</th><th>Status</th><th>Started</th><th>Actions</th></tr></thead><tbody>
      {items.map((item) => <tr key={item.id}>
        <td>{repositoryNames.get(item.repository_id) ?? `#${item.repository_id}`}</td>
        <td><span className="run-id">{item.branch}</span></td>
        <td><code title={item.commit_sha}>{item.commit_sha.slice(0, 10)}</code></td>
        <td><a className="deployment-hostname" href={`https://${item.hostname}`} target="_blank" rel="noreferrer">{item.hostname}</a></td>
        <td><span className={`status status--${item.status}`}>{item.status}</span>{item.error_message && <span className="deployment-error" title={item.error_message}>!</span>}</td>
        <td>{item.started_at ? new Date(item.started_at).toLocaleString() : "—"}</td>
        <td className="actions">
          <a className={item.status === "running" ? "" : "action-disabled"} href={item.status === "running" ? `https://${item.hostname}` : undefined} target="_blank" rel="noreferrer">Open</a>
          <button onClick={() => void runAction(item, "redeploy")} disabled={busy !== null || item.status === "queued" || item.status === "starting"}>Redeploy</button>
          <button onClick={() => void runAction(item, "restart")} disabled={busy !== null || item.status !== "running"}>Restart</button>
          <button onClick={() => void runAction(item, "stop")} disabled={busy !== null || item.status === "stopped"}>Stop</button>
        </td>
      </tr>)}
      {items.length === 0 && <tr><td colSpan={7} className="empty">No branch deployments yet.</td></tr>}
    </tbody></table></div>}
    {creating && <div className="modal-backdrop" role="presentation" onMouseDown={() => setCreating(false)}><form className="new-run" onSubmit={create} onMouseDown={(event) => event.stopPropagation()}><div className="section-heading"><div><span className="eyebrow">Immutable revision</span><h2>Deploy branch</h2></div><button type="button" className="icon-button" onClick={() => setCreating(false)}>×</button></div><label>Repository<select value={repositoryId ?? ""} onChange={(event) => setRepositoryId(Number(event.target.value))} required>{repositories.map((repository) => <option key={repository.id} value={repository.id}>{repository.name}</option>)}</select></label><label>Branch<select name="branch" required>{branches.map((branch) => <option key={branch.name} value={branch.name}>{branch.name} · {branch.commit_sha.slice(0, 7)}</option>)}</select></label><div className="form-actions"><button type="button" className="button button--quiet" onClick={() => setCreating(false)}>Cancel</button><button className="button" disabled={busy === "create" || branches.length === 0}>{busy === "create" ? "Queuing…" : "Deploy"}</button></div></form></div>}
  </main>;
}
