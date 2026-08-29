import { Classes, Icon, Intent, Tag, Tooltip } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { buildInfo, githubCommitUrl, type BuildInfo } from "../buildInfo";
import type { BuildVersion } from "../types/api";

function localBuildTime(value: string) {
  if (value === "unknown") return value;
  const timestamp = new Date(value);
  return Number.isNaN(timestamp.getTime()) ? value : timestamp.toLocaleString();
}

export function metadataMatches(info: BuildInfo, backend: BuildVersion | null) {
  if (backend === null) return true;
  return info.branch === backend.branch && info.commit === backend.commit;
}

export function DeploymentRevision({
  info = buildInfo,
  backendVersion = null,
}: {
  info?: BuildInfo;
  backendVersion?: BuildVersion | null;
}) {
  const commitUrl = githubCommitUrl(info);
  const mismatch = !metadataMatches(info, backendVersion);
  const label = info.commit === "unknown" ? info.branch : `${info.branch} @ ${info.shortCommit}`;
  const details = (
    <div className="revision-tooltip">
      <strong>Deployed revision</strong>
      <span>Branch: {info.branch}</span>
      <span>Commit: {info.commit}</span>
      <span>Built: {localBuildTime(info.builtAt)}</span>
      <span>Host: {info.hostname ?? "unknown"}</span>
      {mismatch && <span className="revision-mismatch"><Icon icon={IconNames.WARNING_SIGN} intent={Intent.WARNING} /> Backend reports {backendVersion?.branch} @ {backendVersion?.short_commit}</span>}
    </div>
  );

  if (commitUrl) {
    return (
      <Tooltip content={details} hoverOpenDelay={150}>
        <a
          aria-label={`Deployed revision ${label}`}
          className={`${Classes.BUTTON} ${Classes.MINIMAL} ${Classes.SMALL} deployment-revision${mismatch ? ` ${Classes.INTENT_WARNING}` : ""}`}
          href={commitUrl}
          target="_blank"
          rel="noreferrer"
        >
          <Icon icon={IconNames.GIT_COMMIT} />
          <span className={Classes.BUTTON_TEXT}>{label}</span>
        </a>
      </Tooltip>
    );
  }

  return (
    <Tooltip content={details} hoverOpenDelay={150}>
      <Tag className="deployment-revision" icon={IconNames.CODE} intent={mismatch ? Intent.WARNING : Intent.NONE} minimal>
        {label}
      </Tag>
    </Tooltip>
  );
}
