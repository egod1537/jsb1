import { Dialog, DialogBody } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import type { ScenarioInspectionDetail } from "../../types/api";
import { ScenarioViewer } from "./ScenarioViewer";

interface Props {
  isOpen: boolean;
  onClose: () => void;
  scenario: ScenarioInspectionDetail | null;
}

export function ScenarioDetailDialog({ isOpen, onClose, scenario }: Props) {
  return <Dialog
    className="scenario-viewer-dialog scenario-library-detail-dialog"
    icon={IconNames.DOCUMENT}
    isOpen={isOpen}
    onClose={onClose}
    title={scenario?.name ?? "Scenario detail"}
  >
    <DialogBody>{scenario && <ScenarioViewer scenario={scenario} />}</DialogBody>
  </Dialog>;
}
