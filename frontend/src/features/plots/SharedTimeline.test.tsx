import { act, render, screen, waitFor } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";
import { buildTimelineTicks, SharedTimeline } from "./SharedTimeline";

const echartsMock = vi.hoisted(() => {
  const handlers = new Map<string, (event: unknown) => void>();
  const zrHandlers = new Map<string, (event: unknown) => void>();
  const chart = {
    setOption: vi.fn(),
    dispatchAction: vi.fn(),
    on: vi.fn((event: string, handler: (payload: unknown) => void) => handlers.set(event, handler)),
    off: vi.fn((event: string) => handlers.delete(event)),
    getZr: () => ({
      on: (event: string, handler: (payload: unknown) => void) => zrHandlers.set(event, handler),
      off: (event: string) => zrHandlers.delete(event),
    }),
    convertFromPixel: vi.fn(() => [0, 0]),
    resize: vi.fn(),
    dispose: vi.fn(),
  };
  return { chart, handlers, zrHandlers };
});

vi.mock("echarts/core", () => ({
  use: vi.fn(),
  init: vi.fn(() => echartsMock.chart),
}));

beforeEach(() => {
  echartsMock.handlers.clear();
  echartsMock.zrHandlers.clear();
  vi.clearAllMocks();
});

describe("SharedTimeline", () => {
  it("reduces tick density while retaining both range edges", () => {
    expect(buildTimelineTicks(0, 30, 800).map((tick) => tick.value)).toEqual([0, 5, 10, 15, 20, 25, 30]);
    expect(buildTimelineTicks(0, 30, 500).map((tick) => tick.value)).toEqual([0, 10, 20, 30]);
    expect(buildTimelineTicks(0, 30, 240).map((tick) => tick.value)).toEqual([0, 15, 30]);
  });

  it("renders the tick axis above a separately padded range selector", async () => {
    render(<SharedTimeline
      fullStart={0}
      fullEnd={30}
      timeline={{ visibleStart: 0.01, visibleEnd: 30, cursorTime: null, selectedRange: null }}
      onVisibleRangeChange={vi.fn()}
      onCursorTimeChange={vi.fn()}
      onSelectedRangeChange={vi.fn()}
    />);

    const axis = screen.getByLabelText("Timeline ticks");
    const selector = screen.getByLabelText("Timeline range selector");
    expect(axis.compareDocumentPosition(selector) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    expect(axis.querySelector(".shared-timeline-tick-first")).toHaveStyle({ left: "0%" });
    expect(axis.querySelector(".shared-timeline-tick-last")).toHaveStyle({ left: "100%" });
    expect(screen.getByText("0.010–30.000 s")).toBeInTheDocument();

    await waitFor(() => expect(echartsMock.chart.setOption).toHaveBeenCalled());
    const option = echartsMock.chart.setOption.mock.calls
      .map(([value]) => value as {
        xAxis?: { show?: boolean };
        dataZoom?: Array<{ left?: number; right?: number; showDetail?: boolean }>;
      })
      .find((value) => value.dataZoom);
    expect(option?.xAxis?.show).toBe(false);
    expect(option?.dataZoom?.[0]).toMatchObject({ left: 18, right: 18, showDetail: false });
    expect(option).not.toHaveProperty("tooltip");
    expect(screen.queryByText(/^t = /)).not.toBeInTheDocument();
  });

  it("updates the shared cursor without rendering a cursor tooltip box", async () => {
    const onCursorTimeChange = vi.fn();
    echartsMock.chart.convertFromPixel.mockReturnValue([2.168, 0]);
    const { rerender } = render(<SharedTimeline
      fullStart={0.01}
      fullEnd={30}
      timeline={{ visibleStart: 0.01, visibleEnd: 30, cursorTime: null, selectedRange: null }}
      onVisibleRangeChange={vi.fn()}
      onCursorTimeChange={onCursorTimeChange}
      onSelectedRangeChange={vi.fn()}
    />);

    await waitFor(() => expect(echartsMock.zrHandlers.has("mousemove")).toBe(true));
    act(() => echartsMock.zrHandlers.get("mousemove")?.({ offsetX: 20, offsetY: 10 }));
    expect(onCursorTimeChange).toHaveBeenCalledWith(2.168);

    rerender(<SharedTimeline
      fullStart={0.01}
      fullEnd={30}
      timeline={{ visibleStart: 0.01, visibleEnd: 30, cursorTime: 2.168, selectedRange: null }}
      onVisibleRangeChange={vi.fn()}
      onCursorTimeChange={onCursorTimeChange}
      onSelectedRangeChange={vi.fn()}
    />);
    expect(screen.getByText("Cursor 2.168 s")).toBeInTheDocument();
    expect(screen.queryByText("t = 2.168 s")).not.toBeInTheDocument();
  });
});
