import { Dialog, DialogBody, Icon, Tag } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import type { Artifact } from "../../types/api";

interface Props {
  artifacts: Artifact[];
  isOpen: boolean;
  onClose: () => void;
  runId: number;
}

export function ArtifactsDialog({ artifacts, isOpen, onClose, runId }: Props) {
  return <Dialog
    className="console-dialog artifacts-dialog"
    icon={IconNames.FOLDER_SHARED_OPEN}
    isOpen={isOpen}
    onClose={onClose}
    title={`Artifacts · Run #${runId}`}
  >
    <DialogBody className="artifacts-dialog-body">
      {artifacts.length === 0 ? <div className="artifacts-empty">No artifacts</div> : <div className="artifact-dialog-list">
        {artifacts.map((artifact) => <a key={artifact.id} href={artifact.download_url}>
          <span className="artifact-dialog-name" title={artifact.filename}>{artifact.filename}</span>
          <Tag className="artifact-kind" minimal>{artifact.kind.toUpperCase()}</Tag>
          <span className="artifact-dialog-action"><Icon icon={IconNames.DOWNLOAD} size={12} />Download</span>
        </a>)}
      </div>}
    </DialogBody>
  </Dialog>;
}
