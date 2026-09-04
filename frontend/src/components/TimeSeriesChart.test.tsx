import { act, render, screen, waitFor } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";
import type { TimelineState } from "../types/view";
import { clampTooltipPosition, formatChartTooltip, TimeSeriesChart, type ChartAnnotations } from "./TimeSeriesChart";

const echartsMock = vi.hoisted(() => {
  const handlers = new Map<string, (event: unknown) => void>();
  const zrHandlers = new Map<string, (event: unknown) => void>();
  const resizeCallbacks: ResizeObserverCallback[] = [];
  const chart = {
    setOption: vi.fn(),
    dispatchAction: vi.fn((action: { type: string }) => {
      if (action.type === "dataZoom") handlers.get("datazoom")?.(action);
      if (action.type === "showTip") handlers.get("updateAxisPointer")?.({ axesInfo: [{ value: 3 }] });
    }),
    on: vi.fn((event: string, handler: (payload: unknown) => void) => handlers.set(event, handler)),
    off: vi.fn((event: string) => handlers.delete(event)),
    getZr: () => ({
      on: (event: string, handler: (payload: unknown) => void) => zrHandlers.set(event, handler),
      off: (event: string) => zrHandlers.delete(event),
    }),
    resize: vi.fn(),
    dispose: vi.fn(),
    group: "",
  };
  return { chart, handlers, resizeCallbacks, zrHandlers };
});

vi.mock("echarts/core", () => ({
  use: vi.fn(),
  init: vi.fn(() => echartsMock.chart),
  connect: vi.fn(),
}));

const initialTimeline: TimelineState = {
  visibleStart: 0,
  visibleEnd: 10,
  cursorTime: null,
  selectedRange: null,
};

beforeEach(() => {
  echartsMock.handlers.clear();
  echartsMock.resizeCallbacks.length = 0;
  echartsMock.zrHandlers.clear();
  vi.clearAllMocks();
  vi.stubGlobal("ResizeObserver", class {
    constructor(callback: ResizeObserverCallback) {
      echartsMock.resizeCallbacks.push(callback);
    }
    observe() {}
    unobserve() {}
    disconnect() {}
  });
});

describe("TimeSeriesChart shared timeline adapter", () => {
  it("clamps tooltip positions and suppresses empty tooltip content", () => {
    expect(clampTooltipPosition([195, 95], {
      viewSize: [200, 100],
      contentSize: [80, 40],
    })).toEqual([103, 43]);
    expect(clampTooltipPosition([-20, -10], {
      viewSize: [200, 100],
      contentSize: [80, 40],
    })).toEqual([6, 6]);
    expect(formatChartTooltip([], "deg")).toBe("");
    expect(formatChartTooltip([{ axisValue: 5, seriesName: "Roll", value: [5, 4.25] }], "deg"))
      .toBe("t = 5.000 s\nRoll  4.250 deg");
  });

  it("renders deterministic analyzer markers and settling band", async () => {
    render(<TimeSeriesChart
      title="Roll tracking"
      unit="deg"
      series={[{ name: "Roll", time: [0, 5, 10], values: [0, 6, 5] }]}
      timeline={initialTimeline}
      annotations={{
        verticalLines: [{ time: 5, label: "Command", emphasized: true }],
        horizontalLines: [{ value: 0.8, label: "Saturation limit", emphasized: true }],
        verticalAreas: [{ start: 7, end: 8, label: "Saturation", emphasized: true }],
        horizontalBands: [{ minimum: 4.5, maximum: 5.5, label: "Settling band", emphasized: true }],
        points: [{ time: 5, value: 6, label: "Peak", emphasized: true }],
      }}
    />);

    await waitFor(() => expect(echartsMock.chart.setOption).toHaveBeenCalled());
    const option = echartsMock.chart.setOption.mock.calls
      .map(([value]) => value as { series?: Array<Record<string, unknown>> })
      .find((value) => value.series?.some((item) => item.id === "__jsb1_chart_annotations__")) as { series: Array<Record<string, unknown>> };
    const telemetry = option.series.find((item) => item.name === "Roll")!;
    const annotationHelper = option.series.find((item) => item.id === "__jsb1_chart_annotations__")!;
    expect(telemetry).not.toHaveProperty("markLine");
    expect(annotationHelper.markLine).toBeTruthy();
    expect(annotationHelper.markArea).toBeTruthy();
    expect(annotationHelper.markPoint).toBeTruthy();
    const markLine = annotationHelper.markLine as { data: Array<{ lineStyle?: { width?: number } }> };
    const markPoint = annotationHelper.markPoint as {
      silent?: boolean;
      data: Array<{
        symbolSize?: number;
        itemStyle?: { borderWidth?: number };
        label?: { show?: boolean };
        tooltip?: { formatter?: string };
      }>;
    };
    const markArea = annotationHelper.markArea as { data: Array<Array<{ itemStyle?: { borderWidth?: number } }>> };
    expect(markLine.data[0].lineStyle?.width).toBe(2);
    expect(annotationHelper.silent).toBe(false);
    expect(markPoint.silent).toBe(false);
    expect(markPoint.data[0].symbolSize).toBe(18);
    expect(markPoint.data[0].itemStyle?.borderWidth).toBe(3);
    expect(markPoint.data[0].label?.show).toBe(true);
    expect(markPoint.data[0].tooltip?.formatter).toBe("Peak\nt = 5.000 s\n6.000°");
    expect(markArea.data[0][0].itemStyle?.borderWidth).toBe(1);
  });

  it("preserves independent legend selections across timeline, annotation, range, series, and resize updates", async () => {
    const chartSeries = [
      { name: "Commanded Roll", time: [0, 5, 10], values: [0, 5, 5] },
      { name: "PX4 / Roll", time: [0, 5, 10], values: [0, 4, 5] },
      { name: "My / Roll", time: [0, 5, 10], values: [0, 4.5, 5] },
    ];
    const renderChart = (
      timeline: TimelineState,
      annotations: ChartAnnotations = { verticalLines: [{ time: 5, label: "Command" }] },
      series = chartSeries,
    ) => <TimeSeriesChart
      annotations={annotations}
      fullTimeRange={[0, 10]}
      series={series}
      timeline={timeline}
      title="Roll"
      unit="deg"
    />;
    const { rerender } = render(renderChart(initialTimeline));
    await waitFor(() => expect(echartsMock.handlers.has("legendselectchanged")).toBe(true));

    type FullOption = {
      legend?: { data?: string[]; selected?: Record<string, boolean> };
      series?: Array<{ id?: string; name?: string; markLine?: unknown; markArea?: { data?: unknown[] } }>;
    };
    const latestFullOption = () => echartsMock.chart.setOption.mock.calls
      .map(([option]) => option as FullOption)
      .filter((option) => option.series)
      .at(-1)!;

    expect(latestFullOption().legend).toMatchObject({
      data: ["Commanded Roll", "PX4 / Roll", "My / Roll"],
      selected: { "Commanded Roll": true, "PX4 / Roll": true, "My / Roll": true },
    });
    expect(latestFullOption().legend?.data).not.toContain("__jsb1_chart_annotations__");

    act(() => echartsMock.handlers.get("legendselectchanged")?.({ selected: { "PX4 / Roll": false } }));
    rerender(renderChart({ ...initialTimeline, visibleStart: 1, visibleEnd: 9 }));
    expect(echartsMock.chart.dispatchAction).toHaveBeenCalledWith(expect.objectContaining({ type: "dataZoom" }));

    rerender(renderChart({ ...initialTimeline, visibleStart: 1, visibleEnd: 9, selectedRange: [2, 4] }));
    expect(latestFullOption().legend?.selected).toEqual({
      "Commanded Roll": true,
      "PX4 / Roll": false,
      "My / Roll": true,
    });

    act(() => echartsMock.handlers.get("legendselectchanged")?.({ selected: { "Commanded Roll": false } }));
    rerender(renderChart(
      { ...initialTimeline, selectedRange: [2, 4] },
      { verticalLines: [{ time: 5, label: "Command", emphasized: true }] },
    ));
    expect(latestFullOption().legend?.selected).toEqual({
      "Commanded Roll": false,
      "PX4 / Roll": false,
      "My / Roll": true,
    });
    const annotationHelper = latestFullOption().series?.find((item) => item.id === "__jsb1_chart_annotations__");
    expect(annotationHelper?.markLine).toBeTruthy();
    expect(annotationHelper?.markArea?.data).toHaveLength(1);

    act(() => echartsMock.handlers.get("legendselectchanged")?.({ selected: { "PX4 / Roll": true, "My / Roll": false } }));
    const chartElement = screen.getByLabelText("Roll chart");
    Object.defineProperty(chartElement, "clientWidth", { configurable: true, value: 640 });
    Object.defineProperty(chartElement, "clientHeight", { configurable: true, value: 320 });
    act(() => echartsMock.resizeCallbacks[0]?.([], {} as ResizeObserver));
    expect(echartsMock.chart.resize).toHaveBeenCalledWith({ width: 640, height: 320 });

    const nextSeries = [chartSeries[0], chartSeries[2], { name: "Filtered Roll", time: [0, 10], values: [0, 5] }];
    rerender(renderChart(
      { ...initialTimeline, cursorTime: 6, selectedRange: [3, 5] },
      { points: [{ time: 5, value: 5.5, label: "Peak", emphasized: true }] },
      nextSeries,
    ));
    expect(latestFullOption().legend?.selected).toEqual({
      "Commanded Roll": false,
      "My / Roll": false,
      "Filtered Roll": true,
    });
    expect(latestFullOption().legend?.selected).not.toHaveProperty("PX4 / Roll");

    act(() => echartsMock.handlers.get("legendselectchanged")?.({ selected: { "Commanded Roll": true, "My / Roll": true } }));
    rerender(renderChart(
      { ...initialTimeline, cursorTime: 6, selectedRange: [4, 6] },
      { horizontalBands: [{ minimum: 4.5, maximum: 5.5, label: "Settling band" }] },
      nextSeries,
    ));
    expect(latestFullOption().legend?.selected).toEqual({
      "Commanded Roll": true,
      "My / Roll": true,
      "Filtered Roll": true,
    });
  });

  it("keeps only inside zoom in a plot and suppresses programmatic zoom echo", async () => {
    const onVisibleRangeChange = vi.fn();
    render(<TimeSeriesChart
      title="Roll"
      unit="deg"
      series={[{ name: "Roll", time: [0, 5, 10], values: [0, 2, 4] }]}
      timeline={initialTimeline}
      fullTimeRange={[0, 10]}
      showLegend={false}
      onVisibleRangeChange={onVisibleRangeChange}
    />);

    await waitFor(() => expect(echartsMock.chart.setOption).toHaveBeenCalled());
    const chartOption = echartsMock.chart.setOption.mock.calls
      .map(([option]) => option as {
        dataZoom?: Array<{ type: string }>;
        legend?: { show?: boolean };
        tooltip?: { appendToBody?: boolean; confine?: boolean; renderMode?: string };
      })
      .find((option) => option.dataZoom);
    expect(chartOption?.dataZoom?.map((zoom) => zoom.type)).toEqual(["inside"]);
    expect(chartOption?.legend?.show).toBe(false);
    expect(chartOption?.tooltip).toMatchObject({
      appendToBody: false,
      confine: true,
      renderMode: "richText",
    });
    expect(onVisibleRangeChange).not.toHaveBeenCalled();

    await act(async () => { await Promise.resolve(); });
    act(() => echartsMock.handlers.get("datazoom")?.({ start: 20, end: 40 }));
    expect(onVisibleRangeChange).toHaveBeenCalledTimes(1);
    expect(onVisibleRangeChange).toHaveBeenCalledWith(2, 4);
  });

  it("suppresses a programmatic cursor echo and forwards a user cursor once", async () => {
    const onCursorTimeChange = vi.fn();
    const { rerender } = render(<TimeSeriesChart
      title="Roll"
      unit="deg"
      series={[{ name: "Roll", time: [0, 3, 10], values: [0, 2, 4] }]}
      timeline={initialTimeline}
      fullTimeRange={[0, 10]}
      onCursorTimeChange={onCursorTimeChange}
    />);

    rerender(<TimeSeriesChart
      title="Roll"
      unit="deg"
      series={[{ name: "Roll", time: [0, 3, 10], values: [0, 2, 4] }]}
      timeline={{ ...initialTimeline, cursorTime: 3 }}
      fullTimeRange={[0, 10]}
      onCursorTimeChange={onCursorTimeChange}
    />);
    expect(onCursorTimeChange).not.toHaveBeenCalled();
    expect(echartsMock.chart.dispatchAction).not.toHaveBeenCalledWith(expect.objectContaining({ type: "showTip" }));
    expect(echartsMock.chart.setOption).toHaveBeenCalledWith(expect.objectContaining({
      xAxis: { axisPointer: { show: true, status: "show", value: 3 } },
    }));

    await act(async () => { await Promise.resolve(); });
    act(() => echartsMock.handlers.get("updateAxisPointer")?.({ axesInfo: [{ value: 4.5 }] }));
    expect(onCursorTimeChange).toHaveBeenCalledTimes(1);
    expect(onCursorTimeChange).toHaveBeenCalledWith(4.5);
  });
});
