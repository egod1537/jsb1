import { FormEvent, useEffect, useState } from "react";
import { Link } from "react-router-dom";
import { api } from "../api/client";
import { ErrorPanel, Loading } from "../components/Loading";
import type { Repository } from "../types/api";

export function RepositoriesPage() {
  const [items, setItems] = useState<Repository[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [creating, setCreating] = useState(false);
  const [busy, setBusy] = useState<number | null>(null);
  const load = () => api.repositories().then(setItems).catch((reason: Error) => setError(reason.message));
  useEffect(() => { load(); }, []); // eslint-disable-line react-hooks/exhaustive-deps

  async function create(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    setError(null);
    const values = new FormData(event.currentTarget);
    try {
      await api.createRepository({
        name: String(values.get("name")),
        remote_url: String(values.get("remote_url")),
        local_path: String(values.get("local_path")),
      });
      setCreating(false);
      load();
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not register repository");
    }
  }

  async function fetchRepository(id: number) {
    setBusy(id);
    setError(null);
    try {
      await api.fetchRepository(id);
      await load();
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Fetch failed");
    } finally {
      setBusy(null);
    }
  }

  return <main>
    <div className="page-heading"><div><span className="eyebrow">Source registry</span><h1>Repositories</h1><p>Canonical JSB0 clones and their current Git state.</p></div><button className="button" onClick={() => setCreating(true)}>＋ Register</button></div>
    {!items && !error && <Loading label="Loading repositories" />}
    {error && <ErrorPanel message={error} />}
    {items && <div className="table-shell"><table><thead><tr><th>Name</th><th>Branch</th><th>Commit</th><th>Dirty</th><th>Last fetch</th><th>Status</th><th>Actions</th></tr></thead><tbody>
      {items.map((item) => <tr key={item.id}><td><Link className="run-id" to={`/repositories/${item.id}`}>{item.name}</Link></td><td>{item.current_branch ?? "detached"}</td><td><code title={item.head_commit}>{item.head_commit.slice(0, 10) || "—"}</code></td><td>{item.dirty ? "Yes" : "No"}</td><td>{item.last_fetched_at ? new Date(item.last_fetched_at).toLocaleString() : "Never"}</td><td>{item.status}</td><td className="actions"><button onClick={() => fetchRepository(item.id)} disabled={busy === item.id}>{busy === item.id ? "Fetching…" : "Fetch"}</button><Link to={`/repositories/${item.id}`}>View / Build</Link></td></tr>)}
      {items.length === 0 && <tr><td colSpan={7} className="empty">No repositories registered.</td></tr>}
    </tbody></table></div>}
    {creating && <div className="modal-backdrop" role="presentation" onMouseDown={() => setCreating(false)}><form className="new-run" onSubmit={create} onMouseDown={(event) => event.stopPropagation()}><div className="section-heading"><div><span className="eyebrow">Canonical clone</span><h2>Register repository</h2></div><button type="button" className="icon-button" onClick={() => setCreating(false)}>×</button></div><label>Name<input name="name" required placeholder="jsb0" /></label><label>Remote URL<input name="remote_url" required placeholder="https://github.com/org/jsb0.git" /></label><label>Local path<input name="local_path" required placeholder="jsb0" /></label><div className="form-actions"><button type="button" className="button button--quiet" onClick={() => setCreating(false)}>Cancel</button><button className="button">Register</button></div></form></div>}
  </main>;
}
