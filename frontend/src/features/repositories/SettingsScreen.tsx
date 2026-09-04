import { Button, ButtonGroup, Callout, HTMLTable, Intent } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { useCallback, useEffect, useState } from "react";
import { Link } from "react-router-dom";
import { api } from "../../api/client";
import { Loading } from "../../components/Loading";
import { PageHeader } from "../../components/PageHeader";
import { StatusTag } from "../../components/StatusTag";
import { showSuccess } from "../../components/toast";
import type { Branch, RuntimeRepository } from "../../types/api";

export function SettingsScreen() {
  const [repository, setRepository] = useState<RuntimeRepository | null>(null);
  const [branches, setBranches] = useState<Branch[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const load = useCallback(async () => {
    setError(null);
    try {
      const runtimeRepository = await api.runtimeRepository();
      setRepository(runtimeRepository);
      setBranches([]);
      const runtimeBranches = await api.runtimeBranches();
      setBranches(runtimeBranches);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not load JSB0 Runtime repository");
    }
  }, []);

  useEffect(() => { void load(); }, [load]);

  async function fetchRepository() {
    setBusy(true);
    setError(null);
    try {
      await api.fetchRuntimeRepository();
      await load();
      void showSuccess("JSB0 Runtime repository fetch completed");
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not fetch JSB0 Runtime repository");
    } finally {
      setBusy(false);
    }
  }

  return <main>
    <PageHeader
      eyebrow="System"
      title="Settings"
      description="Read-only platform configuration and canonical JSB0 Runtime status. Changes are managed through the host environment."
      actions={<ButtonGroup>
        <Button icon={IconNames.REFRESH} minimal onClick={() => void load()}>Refresh</Button>
        <Button icon={IconNames.CLOUD_DOWNLOAD} loading={busy} onClick={() => void fetchRepository()}>Fetch</Button>
      </ButtonGroup>}
    />
    {!repository && !error && <Loading label="Loading JSB0 Runtime repository" />}
    {error && <Callout className="repository-callout" icon={IconNames.ERROR} intent={Intent.DANGER} title="JSB0 Runtime repository unavailable">{error}</Callout>}
    {repository && <>
      <section className="panel repository-status-panel" aria-labelledby="runtime-repository-heading">
        <header className="panel-header">
          <span className="panel-title" id="runtime-repository-heading">Runtime repository</span>
          <StatusTag status={repository.status} />
        </header>
        <dl className="property-grid">
          <div><dt>Repository</dt><dd>{repository.display_name}</dd></div>
          <div><dt>Remote</dt><dd className="technical-value" title={repository.remote_url}>{repository.remote_url}</dd></div>
          <div><dt>Local clone</dt><dd className="technical-value" title={repository.local_path}>{repository.local_path}</dd></div>
          <div><dt>Default branch</dt><dd className="technical-value">{repository.default_branch}</dd></div>
          <div><dt>Current branch</dt><dd className="technical-value">{repository.current_branch ?? "detached"}</dd></div>
          <div><dt>Current HEAD</dt><dd><code title={repository.head_commit}>{repository.head_commit.slice(0, 12) || "—"}</code></dd></div>
          <div><dt>Last fetched</dt><dd className="technical-value">{repository.last_fetched_at ? new Date(repository.last_fetched_at).toLocaleString() : "Never"}</dd></div>
          <div><dt>Configuration source</dt><dd>{repository.configuration_source === "platform" ? "Platform configuration" : repository.configuration_source}</dd></div>
          <div><dt>Working tree</dt><dd><StatusTag status={repository.dirty ? "dirty" : "clean"} /></dd></div>
        </dl>
        {repository.error && <Callout className="repository-callout" icon={IconNames.WARNING_SIGN} intent={repository.status === "error" ? Intent.DANGER : Intent.WARNING}>{repository.error}</Callout>}
      </section>
      <section className="panel repository-branches-panel" aria-labelledby="runtime-branches-heading">
        <header className="panel-header"><span className="panel-title" id="runtime-branches-heading">Remote branches</span><span className="panel-meta">{branches.length}</span></header>
        <div className="table-shell"><HTMLTable compact interactive striped><thead><tr><th>Branch</th><th>Commit</th><th>Source</th><th>Action</th></tr></thead><tbody>
          {branches.map((branch) => <tr key={`${branch.remote ? "remote" : "local"}-${branch.name}`}>
            <td className="technical-value">{branch.name}</td>
            <td><code title={branch.commit_sha}>{branch.commit_sha.slice(0, 12)}</code></td>
            <td>{branch.remote ? "origin" : "local"}</td>
            <td className="actions"><Link to={`/runs?new=1&branch=${encodeURIComponent(branch.name)}`}>Run simulation</Link></td>
          </tr>)}
          {branches.length === 0 && <tr><td colSpan={4} className="empty">No branches are available.</td></tr>}
        </tbody></HTMLTable></div>
      </section>
    </>}
  </main>;
}
