import { Button } from "@blueprintjs/core";
import { useState, type CSSProperties, type DragEvent } from "react";
import type { SignalMetadata } from "../../types/api";
import { PLOT_LAYOUTS, type PlotDataSource, type PlotInstance, type PlotLayoutId, type TimelineState, type WorkspaceTelemetry } from "./plotTypes";
import { PlotPanel } from "./PlotPanel";

interface Props {
  layout: PlotLayoutId;
  plots: PlotInstance[];
  telemetry: WorkspaceTelemetry | null;
  availableSignals: SignalMetadata[];
  timeline: TimelineState;
  onAddPlot: (slot: number) => void;
  onConfigurePlot: (id: string) => void;
  onVisibleRangeChange: (start: number, end: number) => void;
  onCursorTimeChange: (time: number | null) => void;
  onMaximize: (id: string) => void;
  onClose: (id: string) => void;
  onMovePlot: (id: string, targetSlot: number) => void;
  resolveSignal?: PlotDataSource["resolveSignal"];
  hasSignal?: PlotDataSource["hasSignal"];
}

export function PlotGrid(props: Props) {
  const [draggedPlotId, setDraggedPlotId] = useState<string | null>(null);
  const [dropTargetSlot, setDropTargetSlot] = useState<number | null>(null);
  const definition = PLOT_LAYOUTS[props.layout];
  const style = {
    "--plot-columns": definition.columns,
    "--plot-rows": definition.rows,
    "--plot-row-height": `${definition.rowHeight}px`,
  } as CSSProperties;
  const clearDragState = () => {
    setDraggedPlotId(null);
    setDropTargetSlot(null);
  };
  const handleDragStart = (plot: PlotInstance, event: DragEvent<HTMLButtonElement>) => {
    event.dataTransfer.effectAllowed = "move";
    event.dataTransfer.setData("application/x-jsb1-plot", plot.id);
    event.dataTransfer.setData("text/plain", plot.id);
    const panel = event.currentTarget.closest<HTMLElement>(".plot-panel");
    if (panel && event.dataTransfer.setDragImage) event.dataTransfer.setDragImage(panel, 18, 15);
    setDraggedPlotId(plot.id);
  };
  const draggedId = (event: DragEvent<HTMLElement>) =>
    draggedPlotId
    || event.dataTransfer.getData("application/x-jsb1-plot");
  return <div className="plot-grid" style={style} data-layout={props.layout}>
    {Array.from({ length: definition.capacity }, (_, slot) => {
      const plot = props.plots.find((item) => item.slot === slot);
      const isDropTarget = draggedPlotId != null && dropTargetSlot === slot;
      return <div
        className={`plot-grid-cell${isDropTarget ? " plot-grid-cell-drop-target" : ""}`}
        key={slot}
        data-slot={slot}
        data-occupied={plot ? "true" : "false"}
        onDragEnter={(event) => {
          if (!draggedId(event)) return;
          event.preventDefault();
          setDropTargetSlot(slot);
        }}
        onDragOver={(event) => {
          if (!draggedId(event)) return;
          event.preventDefault();
          event.dataTransfer.dropEffect = "move";
          if (dropTargetSlot !== slot) setDropTargetSlot(slot);
        }}
        onDragLeave={(event) => {
          if (event.currentTarget.contains(event.relatedTarget as Node | null)) return;
          setDropTargetSlot((current) => current === slot ? null : current);
        }}
        onDrop={(event) => {
          event.preventDefault();
          const id = draggedId(event);
          if (id) props.onMovePlot(id, slot);
          clearDragState();
        }}
      >
        {!plot ? <div className="plot-empty-slot">
          <Button icon="add" minimal small onClick={() => props.onAddPlot(slot)}>Add Plot</Button>
        </div> : <PlotPanel
          key={plot.id}
          plot={plot}
          telemetry={props.telemetry}
          availableSignals={props.availableSignals}
          timeline={props.timeline}
          dragging={draggedPlotId === plot.id}
          onDragStart={(event) => handleDragStart(plot, event)}
          onDragEnd={clearDragState}
          onConfigure={() => props.onConfigurePlot(plot.id)}
          onVisibleRangeChange={props.onVisibleRangeChange}
          onCursorTimeChange={props.onCursorTimeChange}
          onMaximize={() => props.onMaximize(plot.id)}
          onClose={() => props.onClose(plot.id)}
          resolveSignal={props.resolveSignal}
          hasSignal={props.hasSignal}
        />}
      </div>;
    })}
  </div>;
}
