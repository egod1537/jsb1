import {
  Button,
  ButtonGroup,
  AnchorButton,
  Dialog,
  DialogBody,
  DialogFooter,
  FormGroup,
  HTMLSelect,
  HTMLTable,
  Icon,
  Intent,
  Tooltip,
} from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { FormEvent, useEffect, useMemo, useState } from "react";
import { api } from "../api/client";
import { ConfirmAction } from "../components/ConfirmAction";
import { ErrorPanel, Loading } from "../components/Loading";
import { PageHeader } from "../components/PageHeader";
import { StatusTag } from "../components/StatusTag";
import { showSuccess } from "../components/toast";
import type { Branch, Deployment, Repository } from "../types/api";
import { uniqueBranches } from "../utils/branches";

export function DeploymentsPage() {
  const [items, setItems] = useState<Deployment[] | null>(null);
  const [repositories, setRepositories] = useState<Repository[]>([]);
  const [branches, setBranches] = useState<Branch[]>([]);
  const [repositoryId, setRepositoryId] = useState<number | null>(null);
  const [creating, setCreating] = useState(false);
  const [busy, setBusy] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [pendingStop, setPendingStop] = useState<Deployment | null>(null);
  const [selectedDeploymentId, setSelectedDeploymentId] = useState<number | null>(null);

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
        setBranches(uniqueBranches(records));
      })
      .catch((reason: Error) => setError(reason.message));
  }, [repositoryId]);

  const repositoryNames = useMemo(
    () => new Map(repositories.map((repository) => [repository.id, repository.name])),
    [repositories],
  );
  const selectedDeployment = items?.find((item) => item.id === selectedDeploymentId) ?? null;

  useEffect(() => {
    if (!items?.length) {
      setSelectedDeploymentId(null);
      return;
    }
    setSelectedDeploymentId((current) => items.some((item) => item.id === current) ? current : items[0].id);
  }, [items]);

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
      void showSuccess("Branch deployment queued");
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Deployment could not be queued");
    } finally {
      setBusy(null);
    }
  }

  async function runAction(item: Deployment, action: "redeploy" | "restart" | "stop") {
    const key = `${action}-${item.id}`;
    setBusy(key);
    setError(null);
    try {
      if (action === "redeploy") await api.redeploy(item.id);
      if (action === "restart") await api.restartDeployment(item.id);
      if (action === "stop") await api.stopDeployment(item.id, item.branch === "main");
      await load();
      void showSuccess(`${item.branch} ${action} request completed`);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : `${action} failed`);
    } finally {
      setBusy(null);
      if (action === "stop") setPendingStop(null);
    }
  }

  return <main>
    <PageHeader
      eyebrow="Branch preview routing"
      title="Deployments"
      description="Immutable commit instances behind stable branch hostnames."
      actions={<ButtonGroup>
        <Button icon={IconNames.CLOUD_UPLOAD} intent={Intent.PRIMARY} onClick={() => setCreating(true)} disabled={repositories.length === 0}>Deploy</Button>
        <Button icon={IconNames.REFRESH} minimal onClick={() => void load()}>Refresh</Button>
      </ButtonGroup>}
    />
    {error && <ErrorPanel message={error} />}
    {!items && !error && <Loading label="Loading deployments" />}
    {items && <div className="deployment-workspace">
      <div className="table-shell"><HTMLTable compact interactive striped><thead><tr><th>Repository</th><th>Branch</th><th>Commit</th><th>Hostname</th><th>Status</th><th>Started</th><th>Actions</th></tr></thead><tbody>
        {items.map((item) => <tr
          key={item.id}
          className={selectedDeploymentId === item.id ? "selected-row" : ""}
          aria-selected={selectedDeploymentId === item.id}
          tabIndex={0}
          onClick={() => setSelectedDeploymentId(item.id)}
          onKeyDown={(event) => {
            if (event.key === "Enter" || event.key === " ") setSelectedDeploymentId(item.id);
          }}
        >
          <td>{repositoryNames.get(item.repository_id) ?? `#${item.repository_id}`}</td>
          <td><span className="technical-value">{item.branch}</span></td>
          <td><code title={item.commit_sha}>{item.commit_sha.slice(0, 10)}</code></td>
          <td><a className="deployment-hostname" href={`https://${item.hostname}`} target="_blank" rel="noreferrer" onClick={(event) => event.stopPropagation()}>{item.hostname}</a></td>
          <td><StatusTag status={item.status} />{item.error_message && <Tooltip content={item.error_message}><Icon className="deployment-error" icon={IconNames.ERROR} intent={Intent.DANGER} aria-label="Deployment error" /></Tooltip>}</td>
          <td className="technical-value">{item.started_at ? new Date(item.started_at).toLocaleString() : "—"}</td>
          <td className="actions" onClick={(event) => event.stopPropagation()}><ButtonGroup minimal>
            <Tooltip content="Open deployment"><AnchorButton aria-label={`Open ${item.branch}`} disabled={item.status !== "running"} href={`https://${item.hostname}`} icon={IconNames.SHARE} minimal small target="_blank" rel="noreferrer" /></Tooltip>
            <Tooltip content="Redeploy"><Button aria-label={`Redeploy ${item.branch}`} icon={IconNames.CLOUD_UPLOAD} loading={busy === `redeploy-${item.id}`} small onClick={() => void runAction(item, "redeploy")} disabled={busy !== null || item.status === "queued" || item.status === "starting"} /></Tooltip>
            <Tooltip content="Restart"><Button aria-label={`Restart ${item.branch}`} icon={IconNames.REFRESH} loading={busy === `restart-${item.id}`} small onClick={() => void runAction(item, "restart")} disabled={busy !== null || item.status !== "running"} /></Tooltip>
            <Tooltip content="Stop"><Button aria-label={`Stop ${item.branch}`} icon={IconNames.STOP} intent={Intent.DANGER} small onClick={() => setPendingStop(item)} disabled={busy !== null || item.status === "stopped"} /></Tooltip>
          </ButtonGroup></td>
        </tr>)}
        {items.length === 0 && <tr><td colSpan={7} className="empty">No branch deployments yet.</td></tr>}
      </tbody></HTMLTable></div>
      <aside className="inspector" aria-label="Deployment inspector">
        <header className="panel-header">
          <span className="panel-title">Deployment</span>
          {selectedDeployment && <StatusTag status={selectedDeployment.status} />}
        </header>
        {selectedDeployment ? <>
          <dl className="inspector-properties">
            <div><dt>Branch</dt><dd>{selectedDeployment.branch}</dd></div>
            <div><dt>Commit</dt><dd title={selectedDeployment.commit_sha}>{selectedDeployment.commit_sha.slice(0, 12)}</dd></div>
            <div><dt>Host</dt><dd title={selectedDeployment.hostname}>{selectedDeployment.hostname}</dd></div>
            <div><dt>Frontend port</dt><dd>{selectedDeployment.frontend_port ?? "—"}</dd></div>
            <div><dt>Backend port</dt><dd>{selectedDeployment.backend_port ?? "—"}</dd></div>
            <div><dt>Started</dt><dd>{selectedDeployment.started_at ? new Date(selectedDeployment.started_at).toLocaleString() : "—"}</dd></div>
            <div><dt>Project</dt><dd title={selectedDeployment.compose_project}>{selectedDeployment.compose_project}</dd></div>
          </dl>
          {selectedDeployment.error_message && <div className="inspector-error">{selectedDeployment.error_message}</div>}
          <div className="inspector-actions">
            <AnchorButton disabled={selectedDeployment.status !== "running"} href={`https://${selectedDeployment.hostname}`} icon={IconNames.SHARE} small target="_blank" rel="noreferrer">Open</AnchorButton>
            <Button icon={IconNames.CLOUD_UPLOAD} small onClick={() => void runAction(selectedDeployment, "redeploy")} disabled={busy !== null}>Redeploy</Button>
            <Button icon={IconNames.STOP} intent={Intent.DANGER} minimal small onClick={() => setPendingStop(selectedDeployment)} disabled={busy !== null || selectedDeployment.status === "stopped"}>Stop</Button>
          </div>
        </> : <div className="inspector-empty">Select a deployment row.</div>}
      </aside>
    </div>}
    <Dialog className="console-dialog" icon={IconNames.CLOUD_UPLOAD} isOpen={creating} onClose={() => setCreating(false)} title="Deploy branch">
      <form onSubmit={create}>
        <DialogBody>
          <p className="dialog-intro">Route an immutable branch revision through its stable preview hostname.</p>
          <FormGroup label="Repository" labelFor="deployment-repository"><HTMLSelect id="deployment-repository" fill value={repositoryId ?? ""} onChange={(event) => setRepositoryId(Number(event.currentTarget.value))} required>{repositories.map((repository) => <option key={repository.id} value={repository.id}>{repository.name}</option>)}</HTMLSelect></FormGroup>
          <FormGroup label="Branch" labelFor="deployment-branch"><HTMLSelect id="deployment-branch" fill name="branch" required>{branches.map((branch) => <option key={branch.name} value={branch.name}>{branch.name} · {branch.commit_sha.slice(0, 7)}</option>)}</HTMLSelect></FormGroup>
        </DialogBody>
        <DialogFooter actions={<><Button type="button" onClick={() => setCreating(false)}>Cancel</Button><Button intent={Intent.PRIMARY} loading={busy === "create"} type="submit" disabled={branches.length === 0}>Deploy</Button></>} />
      </form>
    </Dialog>
    <ConfirmAction
      isOpen={pendingStop !== null}
      title={pendingStop?.branch === "main" ? "Stop main deployment?" : `Stop ${pendingStop?.branch ?? "deployment"}?`}
      message={pendingStop?.branch === "main" ? "This explicitly takes the primary site offline and sends force=true." : "The branch preview will no longer be available until it is deployed again."}
      confirmLabel="Stop deployment"
      loading={pendingStop !== null && busy === `stop-${pendingStop.id}`}
      onCancel={() => setPendingStop(null)}
      onConfirm={() => { if (pendingStop) void runAction(pendingStop, "stop"); }}
    />
  </main>;
}
