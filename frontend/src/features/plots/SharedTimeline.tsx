import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import * as echarts from "echarts/core";
import { LineChart } from "echarts/charts";
import {
  DataZoomComponent,
  GridComponent,
} from "echarts/components";
import { CanvasRenderer } from "echarts/renderers";
import type { TimelineState } from "./plotTypes";

echarts.use([
  LineChart,
  DataZoomComponent,
  GridComponent,
  CanvasRenderer,
]);

interface Props {
  fullStart: number;
  fullEnd: number;
  timeline: TimelineState;
  onVisibleRangeChange: (start: number, end: number) => void;
  onCursorTimeChange: (time: number | null) => void;
  onSelectedRangeChange: (range: [number, number] | null) => void;
}

interface DataZoomEvent {
  start?: number;
  end?: number;
  batch?: Array<{ start?: number; end?: number }>;
}

interface PointerEvent {
  offsetX: number;
  offsetY: number;
  event?: { shiftKey?: boolean };
}

function percentage(value: number, start: number, end: number): number {
  if (end <= start) return 0;
  return Math.max(0, Math.min(100, ((value - start) / (end - start)) * 100));
}

function fromPercentage(value: number, start: number, end: number): number {
  return start + ((end - start) * value) / 100;
}

const SELECTOR_EDGE_GUTTER = 18;

export interface TimelineTick {
  value: number;
  label: string;
  position: number;
}

function tickCountForWidth(width: number): number {
  if (width >= 640) return 7;
  if (width >= 320) return 4;
  return 3;
}

function formatTick(value: number, range: number): string {
  const precision = range < 1 ? 3 : range < 10 ? 2 : 1;
  return `${Number(value.toFixed(precision))} s`;
}

export function buildTimelineTicks(start: number, end: number, width: number): TimelineTick[] {
  if (!Number.isFinite(start) || !Number.isFinite(end) || end <= start) return [];
  const count = tickCountForWidth(width);
  return Array.from({ length: count }, (_, index) => {
    const position = index / (count - 1);
    const value = start + ((end - start) * position);
    return {
      value,
      label: formatTick(value, end - start),
      position: position * 100,
    };
  });
}

export function SharedTimeline({
  fullStart,
  fullEnd,
  timeline,
  onVisibleRangeChange,
  onCursorTimeChange,
  onSelectedRangeChange,
}: Props) {
  const element = useRef<HTMLDivElement>(null);
  const chartRef = useRef<echarts.ECharts | null>(null);
  const [selectorWidth, setSelectorWidth] = useState(0);
  const applyingSharedState = useRef(false);
  const sharedUpdateGeneration = useRef(0);
  const selectionAnchor = useRef<number | null>(null);
  const fullRangeRef = useRef<[number, number]>([fullStart, fullEnd]);
  const rangeHandlerRef = useRef(onVisibleRangeChange);
  const cursorHandlerRef = useRef(onCursorTimeChange);
  const selectionHandlerRef = useRef(onSelectedRangeChange);
  fullRangeRef.current = [fullStart, fullEnd];
  rangeHandlerRef.current = onVisibleRangeChange;
  cursorHandlerRef.current = onCursorTimeChange;
  selectionHandlerRef.current = onSelectedRangeChange;

  const guardProgrammaticUpdate = useCallback((update: () => void) => {
    const generation = ++sharedUpdateGeneration.current;
    applyingSharedState.current = true;
    update();
    queueMicrotask(() => {
      if (sharedUpdateGeneration.current === generation) applyingSharedState.current = false;
    });
  }, []);

  useEffect(() => {
    if (!element.current) return;
    const chart = echarts.init(element.current, undefined, { renderer: "canvas" });
    chartRef.current = chart;

    const onDataZoom = (raw: unknown) => {
      if (applyingSharedState.current) return;
      const event = raw as DataZoomEvent;
      const zoom = event.batch?.[0] ?? event;
      if (zoom.start == null || zoom.end == null) return;
      const [start, end] = fullRangeRef.current;
      if (end <= start) return;
      rangeHandlerRef.current(
        fromPercentage(zoom.start, start, end),
        fromPercentage(zoom.end, start, end),
      );
    };
    const valueAtPointer = (event: PointerEvent): number | null => {
      const converted = chart.convertFromPixel(
        { xAxisIndex: 0 },
        [event.offsetX, event.offsetY],
      );
      const value = Number(Array.isArray(converted) ? converted[0] : converted);
      if (!Number.isFinite(value)) return null;
      const [start, end] = fullRangeRef.current;
      return Math.max(start, Math.min(end, value));
    };
    const onMouseDown = (raw: unknown) => {
      const event = raw as PointerEvent;
      if (!event.event?.shiftKey) return;
      selectionAnchor.current = valueAtPointer(event);
      if (selectionAnchor.current != null) {
        selectionHandlerRef.current([
          selectionAnchor.current,
          selectionAnchor.current,
        ]);
      }
    };
    const onMouseMove = (raw: unknown) => {
      const value = valueAtPointer(raw as PointerEvent);
      if (value != null) cursorHandlerRef.current(value);
      if (selectionAnchor.current == null) return;
      if (value == null) return;
      selectionHandlerRef.current([
        Math.min(selectionAnchor.current, value),
        Math.max(selectionAnchor.current, value),
      ]);
    };
    const onMouseUp = () => {
      selectionAnchor.current = null;
    };
    const onGlobalOut = () => {
      selectionAnchor.current = null;
      cursorHandlerRef.current(null);
    };
    const onDoubleClick = () => selectionHandlerRef.current(null);

    chart.on("datazoom", onDataZoom);
    chart.getZr().on("mousedown", onMouseDown);
    chart.getZr().on("mousemove", onMouseMove);
    chart.getZr().on("mouseup", onMouseUp);
    chart.getZr().on("globalout", onGlobalOut);
    chart.getZr().on("dblclick", onDoubleClick);
    const resize = new ResizeObserver(() => {
      const width = element.current?.clientWidth ?? 0;
      const height = element.current?.clientHeight ?? 0;
      if (width > 0) setSelectorWidth(width);
      if (width > 0 && height > 0) chart.resize({ width, height });
    });
    setSelectorWidth(element.current.clientWidth);
    resize.observe(element.current);
    return () => {
      resize.disconnect();
      chart.off("datazoom", onDataZoom);
      chart.getZr().off("mousedown", onMouseDown);
      chart.getZr().off("mousemove", onMouseMove);
      chart.getZr().off("mouseup", onMouseUp);
      chart.getZr().off("globalout", onGlobalOut);
      chart.getZr().off("dblclick", onDoubleClick);
      chart.dispose();
      chartRef.current = null;
    };
  }, []);

  useEffect(() => {
    const chart = chartRef.current;
    if (!chart) return;
    const validRange = fullEnd > fullStart;
    guardProgrammaticUpdate(() => chart.setOption({
      animation: false,
      grid: {
        left: SELECTOR_EDGE_GUTTER,
        right: SELECTOR_EDGE_GUTTER,
        top: 0,
        bottom: 0,
      },
      xAxis: {
        type: "value",
        min: fullStart,
        max: fullEnd,
        show: false,
      },
      yAxis: { type: "value", min: 0, max: 1, show: false },
      dataZoom: validRange ? [{
        type: "slider",
        filterMode: "none",
        left: SELECTOR_EDGE_GUTTER,
        right: SELECTOR_EDGE_GUTTER,
        height: 16,
        top: 4,
        showDetail: false,
        showDataShadow: false,
        brushSelect: false,
        borderColor: "#5f6b7c",
        fillerColor: "rgba(45, 114, 210, 0.28)",
        handleStyle: { color: "#4c90f0" },
        moveHandleStyle: { color: "#2d72d2" },
        start: percentage(timeline.visibleStart, fullStart, fullEnd),
        end: percentage(timeline.visibleEnd, fullStart, fullEnd),
      }] : [],
      series: [{
        id: "shared-timeline-domain",
        type: "line",
        showSymbol: false,
        silent: true,
        lineStyle: { opacity: 0 },
        data: validRange ? [[fullStart, 0], [fullEnd, 0]] : [],
      }],
    }, { notMerge: true }));
  // The range is synchronized by the focused effect below.
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [fullStart, fullEnd, guardProgrammaticUpdate]);

  useEffect(() => {
    const chart = chartRef.current;
    if (!chart || fullEnd <= fullStart) return;
    guardProgrammaticUpdate(() => chart.dispatchAction({
      type: "dataZoom",
      start: percentage(timeline.visibleStart, fullStart, fullEnd),
      end: percentage(timeline.visibleEnd, fullStart, fullEnd),
    }));
  // Callback refs deliberately keep this effect range-only.
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [timeline.visibleStart, timeline.visibleEnd, fullStart, fullEnd, guardProgrammaticUpdate]);

  useEffect(() => {
    const chart = chartRef.current;
    if (!chart) return;
    const selection = timeline.selectedRange;
    chart.setOption({
      series: [{
        id: "shared-timeline-domain",
        markArea: {
          silent: true,
          itemStyle: { color: "rgba(138, 187, 255, 0.12)" },
          data: selection ? [[{ xAxis: selection[0] }, { xAxis: selection[1] }]] : [],
        },
        markLine: {
          silent: true,
          symbol: "none",
          lineStyle: { color: "#8abbff", width: 1 },
          data: timeline.cursorTime == null ? [] : [{ xAxis: timeline.cursorTime }],
        },
      }],
    });
  }, [timeline.cursorTime, timeline.selectedRange]);

  const disabled = fullEnd <= fullStart;
  const ticks = useMemo(
    () => buildTimelineTicks(fullStart, fullEnd, selectorWidth),
    [fullStart, fullEnd, selectorWidth],
  );
  return <footer className="shared-timeline" aria-label="Shared timeline">
    <div className="shared-timeline-labels">
      <span>Timeline</span>
      <span>{disabled
        ? "No samples"
        : `${timeline.visibleStart.toFixed(3)}–${timeline.visibleEnd.toFixed(3)} s`}</span>
      <span>{timeline.cursorTime == null ? "Cursor —" : `Cursor ${timeline.cursorTime.toFixed(3)} s`}</span>
      {timeline.selectedRange && <span>
        Selection {timeline.selectedRange[0].toFixed(3)}–{timeline.selectedRange[1].toFixed(3)} s
      </span>}
      <small>Shift-drag to select · double-click to clear</small>
    </div>
    <div className={`shared-timeline-control${disabled ? " shared-timeline-disabled" : ""}`}>
      <div className="shared-timeline-axis" aria-label="Timeline ticks">
        {ticks.map((tick, index) => <span
          className={index === 0
            ? "shared-timeline-tick shared-timeline-tick-first"
            : index === ticks.length - 1
              ? "shared-timeline-tick shared-timeline-tick-last"
              : "shared-timeline-tick"}
          key={`${tick.position}-${tick.value}`}
          style={{ left: `${tick.position}%` }}
        >{tick.label}</span>)}
      </div>
      <div className="shared-timeline-selector" aria-label="Timeline range selector">
        <div className="shared-timeline-chart" ref={element} />
      </div>
    </div>
  </footer>;
}
