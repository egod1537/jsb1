import { useCallback, useEffect, useReducer, useRef, useState, type ReactNode } from "react";
import { AnalysisWorkspace } from "./AnalysisWorkspace";
import { MaximizedWorkspaceDialog } from "./MaximizedWorkspaceDialog";
import { SharedTimeline } from "./SharedTimeline";
import type { HorizontalReferenceLine, PlotDataSource, PlotLayoutId, TimelineState } from "./plotTypes";
import { createTimeline, timelineReducer } from "./timelineStore";
import { usePlotWorkspaceController } from "./workspaceState";

interface Props {
  dataSource: PlotDataSource;
  scenarioType?: string | null;
  initialLayout?: PlotLayoutId;
  initialPresetId?: string;
  comparisonView?: "overlay" | "side-by-side";
  onComparisonViewChange?: (view: "overlay" | "side-by-side") => void;
  heading?: ReactNode;
  inspector?: ReactNode | ((controls: RunAnalysisControls) => ReactNode);
  acceptanceBandDeg?: number;
  horizontalReferenceLinesByPlot?: Record<string, HorizontalReferenceLine[]>;
}

export interface RunAnalysisControls {
  focusTimeRange: (start: number, end: number) => void;
  timeline: TimelineState;
  onVisibleRangeChange: (start: number, end: number) => void;
  onCursorTimeChange: (time: number | null) => void;
}

export function RunAnalysisView(props: Props) {
  return <TimelineAnalysisContent {...props} />;
}

function TimelineAnalysisContent({
  dataSource,
  scenarioType,
  initialLayout,
  initialPresetId,
  comparisonView,
  onComparisonViewChange,
  heading,
  inspector,
  acceptanceBandDeg,
  horizontalReferenceLinesByPlot,
}: Props) {
  const [timeline, dispatchTimeline] = useReducer(
    timelineReducer,
    undefined,
    () => createTimeline(),
  );
  const [[fullStart, fullEnd], setFullRange] = useState<[number, number]>([0, 0]);
  const fullRangeRef = useRef<[number, number]>([0, 0]);
  const needsInitialRange = useRef(true);
  const [workspaceMaximized, setWorkspaceMaximized] = useState(false);
  const workspaceController = usePlotWorkspaceController({
    scenarioType,
    initialLayout,
    initialPresetId,
    comparisonView,
    variantCount: dataSource.variants?.length,
  });

  useEffect(() => {
    // Reset only Run-level navigation when the source changes. Plot layout and
    // preset remain workspace session state across Run transitions.
    fullRangeRef.current = [0, 0];
    setFullRange([0, 0]);
    needsInitialRange.current = true;
    dispatchTimeline({ type: "reset", start: 0, end: 0 });
  }, [dataSource.key]);

  const handleFullRangeChange = useCallback((start: number, end: number) => {
    fullRangeRef.current = [start, end];
    setFullRange([start, end]);
    if (needsInitialRange.current) {
      needsInitialRange.current = false;
      dispatchTimeline({ type: "reset", start, end });
    }
  }, []);
  const resetView = useCallback(() => {
    dispatchTimeline({
      type: "reset",
      start: fullRangeRef.current[0],
      end: fullRangeRef.current[1],
    });
  }, []);
  const focusTimeRange = useCallback((start: number, end: number) => {
    const [fullRangeStart, fullRangeEnd] = fullRangeRef.current;
    const visibleStart = Math.max(fullRangeStart, Math.min(start, fullRangeEnd));
    const visibleEnd = Math.max(visibleStart, Math.min(end, fullRangeEnd));
    dispatchTimeline({ type: "set-range", start: visibleStart, end: visibleEnd });
    dispatchTimeline({ type: "set-selection", range: [visibleStart, visibleEnd] });
  }, []);
  const workspaceProps = {
    dataSource,
    scenarioType,
    comparisonView,
    onComparisonViewChange,
    workspace: workspaceController,
    timeline,
    onVisibleRangeChange: (start: number, end: number) => dispatchTimeline({ type: "set-range" as const, start, end }),
    onCursorTimeChange: (time: number | null) => dispatchTimeline({ type: "set-cursor" as const, time }),
    onFullTimeRangeChange: handleFullRangeChange,
    onResetView: resetView,
    acceptanceBandDeg,
    horizontalReferenceLinesByPlot,
  };
  const workspace = <AnalysisWorkspace {...workspaceProps} onMaximizeWorkspace={() => setWorkspaceMaximized(true)} />;
  const renderedInspector = typeof inspector === "function"
    ? inspector({
      focusTimeRange,
      timeline,
      onVisibleRangeChange: workspaceProps.onVisibleRangeChange,
      onCursorTimeChange: workspaceProps.onCursorTimeChange,
    })
    : inspector;

  return <section className="run-analysis-view" aria-label="Run analysis">
    <SharedTimeline
      fullStart={fullStart}
      fullEnd={fullEnd}
      timeline={timeline}
      onVisibleRangeChange={(start, end) => dispatchTimeline({ type: "set-range", start, end })}
      onCursorTimeChange={(time) => dispatchTimeline({ type: "set-cursor", time })}
      onSelectedRangeChange={(range) => dispatchTimeline({ type: "set-selection", range })}
    />
    {renderedInspector ? <section className="telemetry-workspace">
      <div className="telemetry-primary">
        {heading}
        {workspace}
      </div>
      {renderedInspector}
    </section> : <>
      {heading}
      {workspace}
    </>}
    <MaximizedWorkspaceDialog isOpen={workspaceMaximized} onClose={() => setWorkspaceMaximized(false)}>
      <AnalysisWorkspace {...workspaceProps} displayMode="dialog" />
    </MaximizedWorkspaceDialog>
  </section>;
}
