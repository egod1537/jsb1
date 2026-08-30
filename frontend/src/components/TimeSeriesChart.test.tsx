import { act, render, waitFor } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";
import type { TimelineState } from "../features/plots/plotTypes";
import { clampTooltipPosition, formatChartTooltip, TimeSeriesChart } from "./TimeSeriesChart";

const echartsMock = vi.hoisted(() => {
  const handlers = new Map<string, (event: unknown) => void>();
  const zrHandlers = new Map<string, (event: unknown) => void>();
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
  return { chart, handlers, zrHandlers };
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
  echartsMock.zrHandlers.clear();
  vi.clearAllMocks();
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
      .find((value) => value.series?.[0]?.markLine) as { series: Array<Record<string, unknown>> };
    expect(option.series[0].markLine).toBeTruthy();
    expect(option.series[0].markArea).toBeTruthy();
    expect(option.series[0].markPoint).toBeTruthy();
    const markLine = option.series[0].markLine as { data: Array<{ lineStyle?: { width?: number } }> };
    const markPoint = option.series[0].markPoint as { data: Array<{ symbolSize?: number }> };
    const markArea = option.series[0].markArea as { data: Array<Array<{ itemStyle?: { borderWidth?: number } }>> };
    expect(markLine.data[0].lineStyle?.width).toBe(2);
    expect(markPoint.data[0].symbolSize).toBe(11);
    expect(markArea.data[0][0].itemStyle?.borderWidth).toBe(1);
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
