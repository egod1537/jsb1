import {
  Button,
  Callout,
  Checkbox,
  Dialog,
  DialogBody,
  DialogFooter,
  HTMLTable,
  Intent,
  Menu,
  MenuDivider,
  MenuItem,
  hideContextMenu,
  showContextMenu,
} from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { useEffect, useState, type MouseEvent } from "react";
import { Link, useNavigate, useSearchParams } from "react-router-dom";
import { api } from "../api/client";
import { showOperationToast, showSuccess } from "../components/toast";
import { ErrorPanel, Loading } from "../components/Loading";
import { NewRunForm } from "../features/runs/NewRunForm";
import { PageHeader } from "../components/PageHeader";
import { StatusTag } from "../components/StatusTag";
import { useRuns } from "../features/runs/useRunData";
import { CurrentStage } from "../features/pipeline/ExecutionPipeline";
import type { RunSummary } from "../types/api";

function time(value: string) {
  return new Intl.DateTimeFormat(undefined, {
    month: "short", day: "2-digit", hour: "2-digit", minute: "2-digit",
  }).format(new Date(value));
}

export function RunsPage() {
  const { data, loading, error, reload } = useRuns();
  const [creating, setCreating] = useState(false);
  const [deleteTarget, setDeleteTarget] = useState<RunSummary | null>(null);
  const [deleting, setDeleting] = useState(false);
  const [deleteError, setDeleteError] = useState<string | null>(null);
  const [selectedRunIds, setSelectedRunIds] = useState<number[]>([]);
  const [searchParams, setSearchParams] = useSearchParams();
  const navigate = useNavigate();
  const requestedBranch = searchParams.get("branch") || undefined;
  const showForm = creating || searchParams.get("new") === "1";
  useEffect(() => {
    if (!data) return;
    const available = new Set(data.map((run) => run.id));
    setSelectedRunIds((current) => current.filter((id) => available.has(id)));
  }, [data]);
  const toggleRunSelection = (runId: number) => setSelectedRunIds((current) => {
    if (current.includes(runId)) return current.filter((id) => id !== runId);
    return [...current, runId];
  });
  const allRunsSelected = data != null
    && data.length > 0
    && data.every((run) => selectedRunIds.includes(run.id));
  const someRunsSelected = data != null
    && data.some((run) => selectedRunIds.includes(run.id))
    && !allRunsSelected;
  const toggleAllRunSelection = () => {
    if (!data) return;
    setSelectedRunIds(allRunsSelected ? [] : data.map((run) => run.id));
  };
  const openContextMenu = (run: RunSummary, event: MouseEvent<HTMLTableRowElement>) => {
    event.preventDefault();
    event.stopPropagation();
    const active = run.status === "queued" || run.status === "running";
    showContextMenu({
      content: <Menu className="run-row-context-menu">
        <MenuItem
          icon={IconNames.DOCUMENT_OPEN}
          text="Open Run"
          onClick={() => {
            hideContextMenu();
            navigate(`/runs/${run.id}`);
          }}
        />
        <MenuDivider />
        <MenuItem
          disabled={active}
          icon={IconNames.TRASH}
          intent={Intent.DANGER}
          text="Delete Run"
          onClick={() => {
            hideContextMenu();
            setDeleteError(null);
            setDeleteTarget(run);
          }}
        />
      </Menu>,
      isDarkTheme: true,
      popoverClassName: "run-row-context-menu-popover",
      targetOffset: { left: event.pageX, top: event.pageY },
    });
  };
  const closeDeleteDialog = () => {
    if (deleting) return;
    setDeleteError(null);
    setDeleteTarget(null);
  };
  const deleteRun = async () => {
    if (!deleteTarget) return;
    setDeleting(true);
    setDeleteError(null);
    try {
      await api.deleteRun(deleteTarget.id);
      await reload();
      void showSuccess(`Run #${deleteTarget.id} deleted`);
      setDeleteTarget(null);
    } catch (reason) {
      const message = reason instanceof Error ? reason.message : "Run deletion failed";
      setDeleteError(message);
      void showOperationToast({ intent: Intent.DANGER, message });
    } finally {
      setDeleting(false);
    }
  };
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
        <>
        <div className="run-selection-toolbar" aria-label="Run comparison selection">
          <span className="technical-value">{selectedRunIds.length} selected</span>
          <Button
            disabled={selectedRunIds.length !== 2}
            icon={IconNames.COMPARISON}
            intent={selectedRunIds.length === 2 ? Intent.PRIMARY : Intent.NONE}
            onClick={() => navigate(`/runs/compare?a=${selectedRunIds[0]}&b=${selectedRunIds[1]}`)}
          >Compare</Button>
        </div>
        <div className="table-shell">
          <HTMLTable compact interactive striped>
            <thead><tr>
              <th className="run-select-cell">
                <Checkbox
                  aria-label="Select all Runs"
                  checked={allRunsSelected}
                  disabled={data.length === 0}
                  indeterminate={someRunsSelected}
                  onChange={toggleAllRunSelection}
                />
              </th><th>ID</th><th>Status</th><th>Scenario</th><th>Repository</th><th>Branch</th><th>Build</th><th>Commit</th>
              <th>Created</th><th>Duration</th>
            </tr></thead>
            <tbody>
              {data.map((run) => (
                <tr
                  key={run.id}
                  className={`run-row${selectedRunIds.includes(run.id) ? " selected-row" : ""}`}
                  onClick={(event) => {
                    if ((event.target as Element).closest("a, button, input, label")) return;
                    navigate(`/runs/${run.id}`);
                  }}
                  onContextMenu={(event) => openContextMenu(run, event)}
                >
                  <td className="run-select-cell" onClick={(event) => event.stopPropagation()}>
                    <Checkbox
                      aria-label={`Select Run #${run.id}`}
                      checked={selectedRunIds.includes(run.id)}
                      onChange={() => toggleRunSelection(run.id)}
                    />
                  </td>
                  <td><Link className="run-id" to={`/runs/${run.id}`}>#{run.id}</Link></td>
                  <td><div className="status-with-stage"><StatusTag status={run.status} /><CurrentStage currentStage={run.current_stage} /></div></td>
                  <td><Link to={`/runs/${run.id}`}>{run.scenario_name}</Link></td>
                  <td>{run.repository_name ?? "Legacy"}</td>
                  <td className="technical-value">{run.branch ?? run.build_branch ?? "—"}</td>
                  <td className="technical-value">{run.build_id ? <Link to={`/builds?selected=${run.build_id}`}>#{run.build_id}</Link> : "—"}</td>
                  <td><code title={run.commit_sha ?? undefined}>{run.commit_sha?.slice(0, 10) ?? "—"}</code></td>
                  <td className="technical-value">{time(run.created_at)}</td>
                  <td className="technical-value">{run.wall_time_sec == null ? "—" : `${run.wall_time_sec.toFixed(2)} s`}</td>
                </tr>
              ))}
              {data.length === 0 && <tr><td colSpan={10} className="empty">No runs yet. Queue the first one.</td></tr>}
            </tbody>
          </HTMLTable>
        </div>
        </>
      )}
      <Dialog
        canEscapeKeyClose={!deleting}
        canOutsideClickClose={!deleting}
        className="console-dialog delete-run-dialog"
        icon={IconNames.TRASH}
        isOpen={deleteTarget != null}
        onClose={closeDeleteDialog}
        title={deleteTarget ? `Delete Run #${deleteTarget.id}?` : "Delete Run"}
      >
        <DialogBody>
          {deleteTarget && <div className="delete-run-confirmation">
            <dl><dt>Scenario</dt><dd>{deleteTarget.scenario_name}</dd></dl>
            <p>This will delete the run record and its stored artifacts.</p>
            {deleteError && <Callout compact intent={Intent.DANGER} role="alert">{deleteError}</Callout>}
          </div>}
        </DialogBody>
        <DialogFooter actions={<>
          <Button disabled={deleting} onClick={closeDeleteDialog}>Cancel</Button>
          <Button intent={Intent.DANGER} loading={deleting} onClick={deleteRun}>Delete</Button>
        </>} />
      </Dialog>
      {showForm && <NewRunForm initialBranch={requestedBranch} onClose={() => { setCreating(false); setSearchParams({}); }} />}
    </main>
  );
}
