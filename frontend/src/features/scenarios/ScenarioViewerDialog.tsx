import { Dialog, DialogBody } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { useEffect, useState } from "react";
import type { ScenarioInspectionDetail } from "../../types/api";
import { ErrorPanel, Loading } from "../../components/Loading";
import { ScenarioViewer } from "./ScenarioViewer";

interface Props {
  isOpen: boolean;
  onClose: () => void;
  load: () => Promise<ScenarioInspectionDetail>;
  title?: string;
}

export function ScenarioViewerDialog({ isOpen, onClose, load, title = "Scenario" }: Props) {
  const [scenario, setScenario] = useState<ScenarioInspectionDetail | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (!isOpen) return;
    let active = true;
    setScenario(null);
    setError(null);
    void load()
      .then((value) => { if (active) setScenario(value); })
      .catch((reason: unknown) => { if (active) setError(reason instanceof Error ? reason.message : "Could not load scenario"); });
    return () => { active = false; };
  }, [isOpen, load]);

  return <Dialog className="console-dialog scenario-viewer-dialog" icon={IconNames.DOCUMENT} isOpen={isOpen} onClose={onClose} title={title}>
    <DialogBody>
      {error ? <ErrorPanel message={error} /> : scenario ? <ScenarioViewer scenario={scenario} /> : <Loading label="Loading scenario" />}
    </DialogBody>
  </Dialog>;
}
