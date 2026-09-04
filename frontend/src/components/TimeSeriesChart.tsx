import { useCallback, useEffect, useRef } from "react";
import * as echarts from "echarts/core";
import { LineChart } from "echarts/charts";
import {
  AxisPointerComponent,
  DataZoomComponent,
  GridComponent,
  LegendComponent,
  TooltipComponent,
} from "echarts/components";
import { CanvasRenderer } from "echarts/renderers";
import type { TimelineState } from "../types/view";

echarts.use([LineChart, AxisPointerComponent, DataZoomComponent, GridComponent, LegendComponent, TooltipComponent, CanvasRenderer]);

export interface ChartSeries {
  name: string;
  time: number[];
  values: number[];
  color?: string;
  dashed?: boolean;
}

export interface ChartAnnotations {
  verticalLines?: Array<{ time: number; label: string; emphasized?: boolean }>;
  horizontalLines?: Array<{ value: number; label: string; emphasized?: boolean }>;
  verticalAreas?: Array<{ start: number; end: number; label: string; color?: string; emphasized?: boolean }>;
  horizontalBands?: Array<{ minimum: number; maximum: number; label: string; emphasized?: boolean }>;
  points?: Array<{ time: number; value: number; label: string; emphasized?: boolean }>;
}

interface Props {
  title: string;
  unit: string;
  series: ChartSeries[];
  showHeader?: boolean;
  timeline?: TimelineState;
  onVisibleRangeChange?: (start: number, end: number) => void;
  onCursorTimeChange?: (time: number | null) => void;
  yAxisMin?: number;
  yAxisMax?: number;
  fullTimeRange?: [number, number];
  showLegend?: boolean;
  annotations?: ChartAnnotations;
}

type EventPayload = {
  start?: number;
  end?: number;
  batch?: Array<{ start?: number; end?: number }>;
  axesInfo?: Array<{ value?: number | string }>;
};

type LegendSelectionPayload = {
  selected?: Record<string, boolean>;
};

type TooltipItem = {
  axisValue?: number | string;
  axisValueLabel?: string;
  seriesName?: string;
  value?: unknown;
};

type TooltipSize = {
  contentSize?: number[];
  viewSize?: number[];
};

const TOOLTIP_MARGIN = 6;
const TOOLTIP_OFFSET = 12;

export function clampTooltipPosition(point: number[], size: TooltipSize): [number, number] {
  const [viewWidth = 0, viewHeight = 0] = size.viewSize ?? [];
  const [contentWidth = 0, contentHeight = 0] = size.contentSize ?? [];
  const preferredX = point[0] + TOOLTIP_OFFSET + contentWidth <= viewWidth
    ? point[0] + TOOLTIP_OFFSET
    : point[0] - contentWidth - TOOLTIP_OFFSET;
  const preferredY = point[1] + TOOLTIP_OFFSET + contentHeight <= viewHeight
    ? point[1] + TOOLTIP_OFFSET
    : point[1] - contentHeight - TOOLTIP_OFFSET;
  return [
    Math.max(TOOLTIP_MARGIN, Math.min(preferredX, Math.max(TOOLTIP_MARGIN, viewWidth - contentWidth - TOOLTIP_MARGIN))),
    Math.max(TOOLTIP_MARGIN, Math.min(preferredY, Math.max(TOOLTIP_MARGIN, viewHeight - contentHeight - TOOLTIP_MARGIN))),
  ];
}

function numericTooltipValue(value: unknown): number | null {
  const candidate = Array.isArray(value) ? value[1] : value;
  const number = Number(candidate);
  return Number.isFinite(number) ? number : null;
}

export function formatChartTooltip(rawItems: unknown, unit: string): string {
  const items = (Array.isArray(rawItems) ? rawItems : [rawItems])
    .filter((item): item is TooltipItem => item != null && typeof item === "object")
    .map((item) => ({ item, value: numericTooltipValue(item.value) }))
    .filter((entry): entry is { item: TooltipItem; value: number } => entry.value != null);
  if (items.length === 0) return "";
  const first = items[0].item;
  const time = first.axisValueLabel ?? (first.axisValue == null ? null : Number(first.axisValue).toFixed(3));
  const lines = time == null ? [] : [`t = ${time} s`];
  for (const { item, value } of items) {
    lines.push(`${item.seriesName ?? "Signal"}  ${value.toFixed(3)}${unit ? ` ${unit}` : ""}`);
  }
  return lines.join("\n");
}

function extent(series: ChartSeries[]): [number, number] {
  let start = Number.POSITIVE_INFINITY;
  let end = Number.NEGATIVE_INFINITY;
  for (const item of series) {
    for (const time of item.time) {
      start = Math.min(start, time);
      end = Math.max(end, time);
    }
  }
  return Number.isFinite(start) && Number.isFinite(end) ? [start, end] : [0, 0];
}

function percentage(value: number, start: number, end: number): number {
  if (end <= start) return 0;
  return Math.max(0, Math.min(100, ((value - start) / (end - start)) * 100));
}

const ANNOTATION_SERIES_ID = "__jsb1_chart_annotations__";

function measurementTooltip(label: string, time: number, value: number, unit: string): string {
  const suffix = unit === "deg" ? "°" : unit === "deg/s" ? "°/s" : unit ? ` ${unit}` : "";
  return `${label}\nt = ${time.toFixed(3)} s\n${value.toFixed(3)}${suffix}`;
}

function annotationSeries(
  annotations: ChartAnnotations | undefined,
  selectedRange: [number, number] | null | undefined,
  unit: string,
) {
  return {
    id: ANNOTATION_SERIES_ID,
    type: "line",
    silent: false,
    showSymbol: false,
    symbol: "none",
    lineStyle: { opacity: 0 },
    tooltip: { show: false },
    data: [],
    markLine: {
      silent: true,
      symbol: "none",
      lineStyle: { color: "#d69e2e", type: "dashed", width: 1 },
      label: { color: "#f0b95f", fontSize: 10, formatter: "{b}", position: "insideEndTop", distance: 3, width: 90, overflow: "truncate" },
      data: [
        ...(annotations?.verticalLines?.map((line) => ({
          name: line.label,
          xAxis: line.time,
          ...(line.emphasized ? {
            lineStyle: { color: "#8abbff", width: 2, opacity: 1 },
            label: { color: "#dbe8fa", fontWeight: 600 },
          } : {}),
        })) ?? []),
        ...(annotations?.horizontalLines?.map((line) => ({
          name: line.label,
          yAxis: line.value,
          ...(line.emphasized ? {
            lineStyle: { color: "#ffb366", width: 2, opacity: 1 },
            label: { color: "#ffe1bf", fontWeight: 600 },
          } : {}),
        })) ?? []),
      ],
    },
    markPoint: {
      silent: false,
      symbol: "circle",
      symbolSize: 8,
      itemStyle: { color: "#f0b95f" },
      label: { show: false },
      data: annotations?.points?.map((point) => ({
        name: point.label,
        coord: [point.time, point.value],
        tooltip: {
          show: true,
          trigger: "item",
          formatter: measurementTooltip(point.label, point.time, point.value, unit),
        },
        ...(point.emphasized ? {
          symbolSize: 18,
          itemStyle: {
            color: "#ff9f43",
            borderColor: "#fff7e8",
            borderWidth: 3,
            shadowBlur: 10,
            shadowColor: "rgba(255, 159, 67, 0.75)",
          },
          label: {
            show: true,
            color: "#fff7e8",
            fontSize: 11,
            fontWeight: 700,
            formatter: "{b}",
            position: "top",
            distance: 8,
            backgroundColor: "rgba(37, 42, 49, 0.92)",
            borderColor: "#ffb366",
            borderWidth: 1,
            borderRadius: 3,
            padding: [3, 5],
          },
        } : {}),
      })) ?? [],
    },
    markArea: {
      silent: true,
      label: { color: "#abb3bf", fontSize: 10, position: "insideTopLeft", width: 110, overflow: "truncate" },
      data: [
        ...(annotations?.verticalAreas?.map((area) => ([
          {
            name: area.label,
            xAxis: area.start,
            itemStyle: {
              color: area.emphasized ? "rgba(76, 144, 240, 0.2)" : area.color ?? "rgba(138, 187, 255, 0.08)",
              ...(area.emphasized ? { borderColor: "rgba(138, 187, 255, 0.65)", borderWidth: 1 } : {}),
            },
            ...(area.emphasized ? { label: { color: "#dbe8fa", fontWeight: 600 } } : {}),
          },
          { xAxis: area.end },
        ])) ?? []),
        ...(annotations?.horizontalBands?.map((band) => ([
          {
            name: band.label,
            yAxis: band.minimum,
            itemStyle: {
              color: band.emphasized ? "rgba(114, 202, 155, 0.2)" : "rgba(114, 202, 155, 0.08)",
              ...(band.emphasized ? { borderColor: "rgba(114, 202, 155, 0.7)", borderWidth: 1 } : {}),
            },
            ...(band.emphasized ? { label: { color: "#d5f5e3", fontWeight: 600 } } : {}),
          },
          { yAxis: band.maximum },
        ])) ?? []),
        ...(selectedRange ? [[
          { xAxis: selectedRange[0], itemStyle: { color: "rgba(138, 187, 255, 0.1)" } },
          { xAxis: selectedRange[1] },
        ]] : []),
      ],
    },
  };
}

export function TimeSeriesChart({
  title,
  unit,
  series,
  showHeader = true,
  timeline,
  onVisibleRangeChange,
  onCursorTimeChange,
  yAxisMin,
  yAxisMax,
  fullTimeRange,
  showLegend = true,
  annotations,
}: Props) {
  const element = useRef<HTMLDivElement>(null);
  const chartRef = useRef<echarts.ECharts | null>(null);
  const domainRef = useRef(fullTimeRange ?? extent(series));
  const rangeHandlerRef = useRef(onVisibleRangeChange);
  const cursorHandlerRef = useRef(onCursorTimeChange);
  const legendSelectionRef = useRef<Record<string, boolean>>({});
  const applyingSharedState = useRef(false);
  const sharedUpdateGeneration = useRef(0);
  domainRef.current = fullTimeRange ?? extent(series);
  rangeHandlerRef.current = onVisibleRangeChange;
  cursorHandlerRef.current = onCursorTimeChange;

  const [start, end] = domainRef.current;
  const range = end > start ? `${start.toFixed(1)}–${end.toFixed(1)} s` : "No samples";
  const applySharedState = useCallback((update: () => void) => {
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
      const event = raw as EventPayload;
      const zoom = event.batch?.[0] ?? event;
      if (zoom.start == null || zoom.end == null) return;
      const [minimum, maximum] = domainRef.current;
      if (maximum <= minimum) return;
      rangeHandlerRef.current?.(
        minimum + ((maximum - minimum) * zoom.start) / 100,
        minimum + ((maximum - minimum) * zoom.end) / 100,
      );
    };
    const onAxisPointer = (raw: unknown) => {
      if (applyingSharedState.current) return;
      const value = Number((raw as EventPayload).axesInfo?.[0]?.value);
      if (Number.isFinite(value)) cursorHandlerRef.current?.(value);
    };
    const onPointerLeave = () => {
      chart.dispatchAction({ type: "hideTip" });
      if (!applyingSharedState.current) cursorHandlerRef.current?.(null);
    };
    const onLegendSelectionChanged = (raw: unknown) => {
      const selected = (raw as LegendSelectionPayload).selected;
      if (selected && typeof selected === "object") {
        legendSelectionRef.current = { ...legendSelectionRef.current, ...selected };
      }
    };
    chart.on("datazoom", onDataZoom);
    chart.on("updateAxisPointer", onAxisPointer);
    chart.on("legendselectchanged", onLegendSelectionChanged);
    chart.getZr().on("globalout", onPointerLeave);
    const resize = new ResizeObserver(() => {
      const width = element.current?.clientWidth ?? 0;
      const height = element.current?.clientHeight ?? 0;
      if (width > 0 && height > 0) {
        chart.dispatchAction({ type: "hideTip" });
        chart.resize({ width, height });
      }
    });
    resize.observe(element.current);
    return () => {
      resize.disconnect();
      chart.getZr().off("globalout", onPointerLeave);
      chart.off("datazoom", onDataZoom);
      chart.off("updateAxisPointer", onAxisPointer);
      chart.off("legendselectchanged", onLegendSelectionChanged);
      chart.dispose();
      chartRef.current = null;
    };
  }, []);

  useEffect(() => {
    const chart = chartRef.current;
    if (!chart) return;
    const [minimum, maximum] = fullTimeRange ?? extent(series);
    const visibleStart = timeline?.visibleStart ?? minimum;
    const visibleEnd = timeline?.visibleEnd ?? maximum;
    const legendSelection = Object.fromEntries(series.map((item) => [
      item.name,
      legendSelectionRef.current[item.name] ?? true,
    ]));
    legendSelectionRef.current = legendSelection;
    applySharedState(() => chart.setOption({
      animation: false,
      color: series.map((item) => item.color).filter((color): color is string => Boolean(color)),
      legend: {
        show: showLegend,
        data: series.map((item) => item.name),
        selected: legendSelection,
        right: 10,
        top: 5,
        textStyle: { color: "#abb3bf", fontSize: 10 },
        itemWidth: 14,
        itemHeight: 7,
        icon: "roundRect",
      },
      grid: { left: 54, right: 16, top: showLegend ? 32 : 10, bottom: 31 },
      tooltip: {
        trigger: "axis",
        renderMode: "richText",
        appendToBody: false,
        confine: true,
        enterable: false,
        hideDelay: 0,
        transitionDuration: 0,
        position: (point: number[], _params: unknown, _element: unknown, _rect: unknown, size: TooltipSize) => clampTooltipPosition(point, size),
        formatter: (items: unknown) => formatChartTooltip(items, unit),
        axisPointer: { type: "line", lineStyle: { color: "#8abbff", width: 1 } },
        backgroundColor: "rgba(37, 42, 49, 0.96)",
        borderColor: "#5f6b7c",
        textStyle: { color: "#f6f7f9", fontFamily: "DM Mono", fontSize: 10 },
      },
      xAxis: {
        type: "value", name: "time (s)", nameLocation: "middle", nameGap: 25, min: minimum, max: maximum,
        axisPointer: { show: true, snap: false },
        axisLine: { lineStyle: { color: "#5f6b7c" } },
        axisLabel: { color: "#abb3bf", fontSize: 10 },
        splitLine: { lineStyle: { color: "#383e47" } },
      },
      yAxis: {
        type: "value", name: unit, min: yAxisMin, max: yAxisMax,
        nameTextStyle: { color: "#8f99a8", fontSize: 10 },
        axisLabel: { color: "#abb3bf", fontSize: 10 },
        splitLine: { lineStyle: { color: "#383e47" } },
      },
      dataZoom: [
        { type: "inside", filterMode: "none", start: percentage(visibleStart, minimum, maximum), end: percentage(visibleEnd, minimum, maximum), moveOnMouseMove: true },
      ],
      series: [
        ...series.map((item) => ({
          id: item.name, name: item.name, type: "line", showSymbol: false, sampling: "lttb",
          lineStyle: { width: 1.4, type: item.dashed ? "dashed" : "solid" },
          labelLayout: { hideOverlap: true, moveOverlap: "shiftY" },
          data: item.time.map((time, index) => [time, item.values[index]]),
        })),
        annotationSeries(annotations, timeline?.selectedRange, unit),
      ],
    }, { notMerge: true }));
  }, [series, unit, yAxisMin, yAxisMax, fullTimeRange, showLegend, annotations, timeline?.selectedRange, applySharedState]);

  useEffect(() => {
    const chart = chartRef.current;
    if (!chart || !timeline || end <= start) return;
    applySharedState(() => chart.dispatchAction({
      type: "dataZoom",
      start: percentage(timeline.visibleStart, start, end),
      end: percentage(timeline.visibleEnd, start, end),
    }));
  }, [timeline?.visibleStart, timeline?.visibleEnd, start, end, applySharedState]);

  useEffect(() => {
    const chart = chartRef.current;
    if (!chart) return;
    applySharedState(() => {
      chart.setOption({
        xAxis: {
          axisPointer: timeline?.cursorTime == null
            ? { show: true, status: "hide" }
            : { show: true, status: "show", value: timeline.cursorTime },
        },
      });
    });
  }, [timeline?.cursorTime, applySharedState]);

  const chartElement = <div className="chart" ref={element} aria-label={`${title} chart`} />;
  if (!showHeader) return chartElement;
  return <section className="chart-panel">
    <header className="panel-header chart-panel-header">
      <div><span className="panel-title">{title}</span><span className="chart-unit">{unit}</span></div>
      <span className="chart-range">{range}</span>
    </header>
    {chartElement}
  </section>;
}
