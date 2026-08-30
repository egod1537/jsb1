import { Dialog, DialogBody } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import type { SignalMetadata } from "../../types/api";
import type { PlotDataSource, PlotInstance, TimelineState, WorkspaceTelemetry } from "./plotTypes";
import { PlotPanel } from "./PlotPanel";

interface Props {
  plot: PlotInstance | null;
  telemetry: WorkspaceTelemetry | null;
  availableSignals: SignalMetadata[];
  timeline: TimelineState;
  onVisibleRangeChange: (start: number, end: number) => void;
  onCursorTimeChange: (time: number | null) => void;
  onClose: () => void;
  resolveSignal?: PlotDataSource["resolveSignal"];
  hasSignal?: PlotDataSource["hasSignal"];
}

export function MaximizedPlotDialog({
  plot,
  telemetry,
  availableSignals,
  timeline,
  onVisibleRangeChange,
  onCursorTimeChange,
  onClose,
  resolveSignal,
  hasSignal,
}: Props) {
  return <Dialog
    className="plot-maximize-dialog"
    icon={IconNames.MAXIMIZE}
    isOpen={plot != null}
    onClose={onClose}
    title={plot ? `${plot.title} · Expanded plot` : "Expanded plot"}
  >
    {plot && <DialogBody className="plot-maximize-dialog-body">
      <PlotPanel
        plot={plot}
        telemetry={telemetry}
        availableSignals={availableSignals}
        timeline={timeline}
        displayMode="dialog"
        onVisibleRangeChange={onVisibleRangeChange}
        onCursorTimeChange={onCursorTimeChange}
        resolveSignal={resolveSignal}
        hasSignal={hasSignal}
      />
    </DialogBody>}
  </Dialog>;
}
