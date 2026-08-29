import { FormEvent, useEffect, useState } from "react";
import { Link, useNavigate, useParams } from "react-router-dom";
import { api } from "../api/client";
import { ErrorPanel, Loading } from "../components/Loading";
import { StatusBadge } from "../components/StatusBadge";
import type { Branch, Build, Repository } from "../types/api";

export function RepositoryDetailPage() {
  const id = Number(useParams().id);
  const navigate = useNavigate();
  const [repository, setRepository] = useState<Repository | null>(null);
  const [branches, setBranches] = useState<Branch[]>([]);
  const [builds, setBuilds] = useState<Build[]>([]);
  const [revision, setRevision] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    Promise.all([api.repository(id), api.branches(id), api.builds(id)])
      .then(([repo, refs, recent]) => {
        setRepository(repo);
        setBranches(refs);
        setBuilds(recent);
        setRevision(repo.current_branch ?? repo.default_branch);
      })
      .catch((reason: Error) => setError(reason.message));
  }, [id]);

  async function build(event: FormEvent) {
    event.preventDefault();
    setBusy(true);
    setError(null);
    try {
      const item = await api.createBuild({ repository_id: id, revision });
      navigate(`/builds?selected=${item.id}`);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Build request failed");
      setBusy(false);
    }
  }

  if (error && !repository) return <main><ErrorPanel message={error} /></main>;
  if (!repository) return <main><Loading label="Loading repository" /></main>;
  return <main>
    <Link className="back-link" to="/repositories">← All repositories</Link>
    <div className="page-heading page-heading--detail"><div><span className="eyebrow">{repository.local_path}</span><h1>{repository.name}</h1><p>{repository.remote_url}</p></div><span className={`repo-state ${repository.dirty ? "repo-state--dirty" : ""}`}>{repository.dirty ? "Dirty" : "Clean"}</span></div>
    {error && <ErrorPanel message={error} />}
    <section className="run-facts"><div><span>Current branch</span><strong>{repository.current_branch ?? "detached"}</strong></div><div><span>HEAD commit</span><code title={repository.head_commit}>{repository.head_commit.slice(0, 10)}</code></div><div><span>Default branch</span><strong>{repository.default_branch}</strong></div><div><span>Last fetch</span><strong>{repository.last_fetched_at ? new Date(repository.last_fetched_at).toLocaleString() : "Never"}</strong></div></section>
    <form className="build-request" onSubmit={build}><div><span className="eyebrow">Immutable revision</span><h2>Create build</h2></div><select value={revision} onChange={(event) => setRevision(event.target.value)}>{branches.map((branch) => <option key={`${branch.remote ? "remote" : "local"}-${branch.name}`} value={branch.remote ? `origin/${branch.name}` : branch.name}>{branch.remote ? `origin/${branch.name}` : branch.name} · {branch.commit_sha.slice(0, 10)}</option>)}</select><button className="button" disabled={busy || !revision}>{busy ? "Queuing…" : "Build revision"}</button></form>
    <section><div className="section-heading"><h2>Recent builds</h2></div><div className="table-shell"><table><thead><tr><th>ID</th><th>Revision</th><th>Commit</th><th>Status</th><th>Created</th></tr></thead><tbody>{builds.map((item) => <tr key={item.id}><td><Link className="run-id" to={`/builds?selected=${item.id}`}>#{item.id}</Link></td><td>{item.branch ?? "detached"}</td><td><code title={item.commit_sha}>{item.commit_sha.slice(0, 10)}</code></td><td><StatusBadge status={item.status} /></td><td>{new Date(item.created_at).toLocaleString()}</td></tr>)}{builds.length === 0 && <tr><td colSpan={5} className="empty">No builds yet.</td></tr>}</tbody></table></div></section>
  </main>;
}
