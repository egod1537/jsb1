import { AnchorButton, Dialog, DialogBody, Tag } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { ErrorPanel, Loading } from "../../components/Loading";
import { StatusTag } from "../../components/StatusTag";
import type { Build } from "../../types/api";
import { BUILD_PIPELINE_GROUPS, ExecutionPipeline } from "../pipeline/ExecutionPipeline";

interface Props {
  build: Build | null;
  buildId: number | null;
  error: string | null;
  isOpen: boolean;
  loading: boolean;
  onClose: () => void;
  reused?: boolean;
}

function displayTime(value: string | null): string {
  return value ? new Date(value).toLocaleString() : "—";
}

function duration(build: Build): string {
  if (!build.started_at || !build.completed_at) return "—";
  const elapsed = (new Date(build.completed_at).getTime() - new Date(build.started_at).getTime()) / 1000;
  return `${elapsed.toFixed(2)} s`;
}

export function BuildDetailsDialog({ build, buildId, error, isOpen, loading, onClose, reused = false }: Props) {
  return <Dialog
    className="build-detail-dialog"
    icon={IconNames.BUILD}
    isOpen={isOpen}
    onClose={onClose}
    title={buildId == null ? "Build detail" : `Build #${buildId}`}
  >
    <DialogBody className="build-detail-dialog-body">
      {loading && <Loading label="Loading build detail" />}
      {error && <ErrorPanel message={error} />}
      {build && <>
        <section className="build-detail-heading" aria-label="Build identity">
          <div><span>Runtime</span><strong>{build.repository_name ?? `Repository #${build.repository_id}`}</strong></div>
          <div><span>Revision</span><strong>{build.branch ?? "detached"}</strong></div>
          <div><span>Immutable commit</span><code title={build.commit_sha}>{build.commit_sha}</code></div>
          <div><span>Status</span><div className="build-detail-status"><StatusTag status={build.status} />{(reused || build.reused) && <Tag minimal>REUSED</Tag>}</div></div>
          <div><span>Created</span><strong>{displayTime(build.created_at)}</strong></div>
          <div><span>Started</span><strong>{displayTime(build.started_at)}</strong></div>
          <div><span>Finished</span><strong>{displayTime(build.completed_at)}</strong></div>
          <div><span>Duration</span><strong>{duration(build)}</strong></div>
        </section>

        <ExecutionPipeline stages={build.stages ?? []} groups={BUILD_PIPELINE_GROUPS} title="Build pipeline" />

        <section className="build-output-panel panel" aria-label="Build outputs">
          <header className="panel-header"><span className="panel-title">Build outputs</span></header>
          <dl>
            <div><dt>Build directory</dt><dd><code title={build.build_dir}>{build.build_dir}</code></dd></div>
            <div><dt>Executable</dt><dd><code title={build.executable_path ?? undefined}>{build.executable_path ?? "Not available"}</code></dd></div>
          </dl>
          <div className="build-log-actions">
            <AnchorButton href={`/api/builds/${build.id}/logs/stdout`} icon={IconNames.DOCUMENT_OPEN} small target="_blank">View stdout log</AnchorButton>
            <AnchorButton href={`/api/builds/${build.id}/logs/stderr`} icon={IconNames.DOCUMENT_OPEN} small target="_blank">View stderr log</AnchorButton>
          </div>
        </section>
        {build.error_message && <ErrorPanel message={build.error_message} />}
      </>}
    </DialogBody>
  </Dialog>;
}
