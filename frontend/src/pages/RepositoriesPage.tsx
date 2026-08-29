import {
  Button,
  Dialog,
  DialogBody,
  DialogFooter,
  FormGroup,
  HTMLTable,
  InputGroup,
  Intent,
} from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { FormEvent, useEffect, useState } from "react";
import { Link } from "react-router-dom";
import { api } from "../api/client";
import { ErrorPanel, Loading } from "../components/Loading";
import { PageHeader } from "../components/PageHeader";
import { StatusTag } from "../components/StatusTag";
import { showSuccess } from "../components/toast";
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
      void showSuccess("Repository registered");
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
      void showSuccess("Repository fetch completed");
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Fetch failed");
    } finally {
      setBusy(null);
    }
  }

  return <main>
    <PageHeader
      eyebrow="Source registry"
      title="Repositories"
      description="Canonical JSB0 clones and their current Git state."
      actions={<Button icon={IconNames.ADD} intent={Intent.PRIMARY} onClick={() => setCreating(true)}>Register</Button>}
    />
    {!items && !error && <Loading label="Loading repositories" />}
    {error && <ErrorPanel message={error} />}
    {items && <div className="table-shell"><HTMLTable compact interactive striped><thead><tr><th>Name</th><th>Branch</th><th>Commit</th><th>Working tree</th><th>Last fetch</th><th>Status</th><th>Actions</th></tr></thead><tbody>
      {items.map((item) => <tr key={item.id}><td><Link className="run-id" to={`/repositories/${item.id}`}>{item.name}</Link></td><td className="technical-value">{item.current_branch ?? "detached"}</td><td><code title={item.head_commit}>{item.head_commit.slice(0, 10) || "—"}</code></td><td><StatusTag status={item.dirty ? "dirty" : "clean"} /></td><td className="technical-value">{item.last_fetched_at ? new Date(item.last_fetched_at).toLocaleString() : "Never"}</td><td><StatusTag status={item.status} /></td><td className="actions"><div className="action-group"><Button icon={IconNames.CLOUD_DOWNLOAD} loading={busy === item.id} minimal small onClick={() => fetchRepository(item.id)}>Fetch</Button><Link to={`/repositories/${item.id}`}>View / Build</Link></div></td></tr>)}
      {items.length === 0 && <tr><td colSpan={7} className="empty">No repositories registered.</td></tr>}
    </tbody></HTMLTable></div>}
    <Dialog className="console-dialog" icon={IconNames.GIT_REPO} isOpen={creating} onClose={() => setCreating(false)} title="Register repository">
      <form onSubmit={create}>
        <DialogBody>
          <p className="dialog-intro">Register a canonical source clone for revision builds.</p>
          <FormGroup label="Name" labelFor="repository-name"><InputGroup id="repository-name" name="name" required placeholder="jsb0" /></FormGroup>
          <FormGroup label="Remote URL" labelFor="repository-remote"><InputGroup id="repository-remote" name="remote_url" required placeholder="https://github.com/org/jsb0.git" /></FormGroup>
          <FormGroup label="Local path" labelFor="repository-path"><InputGroup id="repository-path" name="local_path" required placeholder="jsb0" /></FormGroup>
        </DialogBody>
        <DialogFooter actions={<><Button type="button" onClick={() => setCreating(false)}>Cancel</Button><Button intent={Intent.PRIMARY} type="submit">Register</Button></>} />
      </form>
    </Dialog>
  </main>;
}
