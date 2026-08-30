import { Dialog, DialogBody } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import type { ReactNode } from "react";

interface Props {
  isOpen: boolean;
  onClose: () => void;
  children: ReactNode;
}

export function MaximizedWorkspaceDialog({ isOpen, onClose, children }: Props) {
  return <Dialog
    className="workspace-maximize-dialog"
    icon={IconNames.FULLSCREEN}
    isOpen={isOpen}
    onClose={onClose}
    title="Analysis workspace"
  >
    <DialogBody className="workspace-maximize-dialog-body">{children}</DialogBody>
  </Dialog>;
}
