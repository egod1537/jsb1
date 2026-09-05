import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { api, ApiError } from "../../api/client";
import type { SignalMetadata, SignalResponse } from "../../types/api";
import { RunAnalysisView as AnalysisWorkspace } from "./RunAnalysisView";
import { createRunComparisonDataSource } from "./runComparisonDataSource";
import { PLOT_TEMPLATES, validatePlotTemplateRegistry } from "./plotTemplates";
import { PLOT_LAYOUTS, createPresetPlots, signalUnion } from "./plotTypes";
import {
  ANALYSIS_PRESETS,
  COURSE_HOLD_PRESET,
  DYNAMICS_PRESET,
  PITCH_HOLD_PRESET,
  ROLL_HOLD_PRESET,
  TECS_PRESET,
  defaultPresetForScenario,
  presetAvailability,
  validatePresetRegistry,
} from "./presets";
import { loadRunSignalUnion } from "./runSignalDataSource";
import { groupSignalDefinitions, normalizeSignalMetadata } from "./signalCatalog";
import { createTimeline, timelineReducer } from "./timelineStore";

vi.mock("../../components/TimeSeriesChart", () => ({
  TimeSeriesChart: ({
    title,
    timeline,
    onVisibleRangeChange,
    onCursorTimeChange,
    showLegend,
    yAxisMin,
    yAxisMax,
    series,
  }: {
    title: string;
    timeline: { visibleStart: number; visibleEnd: number; cursorTime: number | null; selectedRange: [number, number] | null };
    onVisibleRangeChange: (start: number, end: number) => void;
    onCursorTimeChange: (time: number | null) => void;
    showLegend?: boolean;
    yAxisMin?: number;
    yAxisMax?: number;
    series: Array<{ name: string; values: number[] }>;
  }) => <div
    data-testid="synchronized-chart"
    aria-label={`${title} chart`}
    data-title={title}
    data-start={timeline.visibleStart}
    data-end={timeline.visibleEnd}
    data-cursor={timeline.cursorTime ?? "none"}
    data-selection={timeline.selectedRange?.join(":") ?? "none"}
    data-show-legend={String(showLegend ?? true)}
    data-y-min={yAxisMin ?? "auto"}
    data-y-max={yAxisMax ?? "auto"}
    data-series={JSON.stringify(series)}
  >
    <button aria-label={`zoom ${title}`} onClick={() => onVisibleRangeChange(2, 4)}>zoom</button>
    <button aria-label={`cursor ${title}`} onClick={() => onCursorTimeChange(3)}>cursor</button>
  </div>,
}));

vi.mock("./SharedTimeline", () => ({
  SharedTimeline: ({
    timeline,
    onVisibleRangeChange,
    onCursorTimeChange,
    onSelectedRangeChange,
  }: {
    timeline: { visibleStart: number; visibleEnd: number; cursorTime: number | null; selectedRange: [number, number] | null };
    onVisibleRangeChange: (start: number, end: number) => void;
    onCursorTimeChange: (time: number | null) => void;
    onSelectedRangeChange: (range: [number, number] | null) => void;
  }) => <footer
    className="shared-timeline"
    aria-label="Shared timeline"
    data-start={timeline.visibleStart}
    data-end={timeline.visibleEnd}
    data-cursor={timeline.cursorTime ?? "none"}
    data-selection={timeline.selectedRange?.join(":") ?? "none"}
  >
    <button aria-label="timeline range" onClick={() => onVisibleRangeChange(1, 4)}>range</button>
    <button aria-label="timeline cursor" onClick={() => onCursorTimeChange(2.5)}>cursor</button>
    <button aria-label="timeline selection" onClick={() => onSelectedRangeChange([2, 3])}>selection</button>
  </footer>,
}));

const CONTRACT_SIGNALS: SignalMetadata[] = [
  { name: "commanded_roll", display_name: "Commanded Roll", symbol: "φc", symbol_latex: "\\phi_c", unit: "deg", category: "Command", subcategory: "Roll", description: "Roll demand." },
  { name: "commanded_roll_rate", display_name: "Commanded Roll Rate", unit: "deg/s", category: "Command", subcategory: "Roll" },
  { name: "roll", display_name: "Roll", symbol: "φ", symbol_latex: "\\phi", unit: "deg", category: "Aircraft State", subcategory: "Attitude" },
  { name: "roll_rate", display_name: "Roll Rate", unit: "deg/s", category: "Aircraft State", subcategory: "Angular Rates" },
  { name: "roll_error", display_name: "Roll Error", unit: "deg", category: "Control", subcategory: "Tracking Error" },
  { name: "roll_rate_error", display_name: "Roll Rate Error", unit: "deg/s", category: "Control", subcategory: "Tracking Error" },
  { name: "aileron", display_name: "Aileron", symbol: "δa", symbol_latex: "\\delta_a", unit: "normalized", group: "Control.Surfaces" },
];

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

const fullTelemetry: SignalResponse = {
  time: [0, 1, 2, 3, 4, 5],
  series: {
    commanded_roll: [0, 0, 5, 5, 5, 5],
    roll: [0, 0, 2, 4, 5, 5],
    commanded_roll_rate: [0, 0, 1, 0, 0, 0],
    roll_rate: [0, 0, 0.8, 0.4, 0.1, 0],
    roll_error: [0, 0, 3, 1, 0, 0],
    roll_rate_error: [0, 0, 0.2, -0.4, -0.1, 0],
    aileron: [0, 0, 0.2, 0.1, 0.05, 0],
  },
  units: {
    commanded_roll: "deg",
    roll: "deg",
    commanded_roll_rate: "deg/s",
    roll_rate: "deg/s",
    roll_error: "deg",
    roll_rate_error: "deg/s",
    aileron: "normalized",
  },
  source_points: 6,
  returned_points: 6,
};

function dataSource(telemetry = fullTelemetry) {
  const metadata = Object.keys(telemetry.series).map((name) => CONTRACT_SIGNALS.find((signal) => signal.name === name) ?? ({
    name,
    unit: telemetry.units[name] ?? "raw",
  }));
  return {
    key: "run-42",
    loadSignals: vi.fn().mockResolvedValue(telemetry),
    listSignals: vi.fn().mockResolvedValue(metadata),
  };
}

function dragDataTransfer(): DataTransfer {
  const values = new Map<string, string>();
  return {
    dropEffect: "none",
    effectAllowed: "all",
    files: [] as unknown as FileList,
    items: [] as unknown as DataTransferItemList,
    types: [],
    clearData: (type?: string) => type ? values.delete(type) : values.clear(),
    getData: (type: string) => values.get(type) ?? "",
    setData: (type: string, value: string) => { values.set(type, value); },
    setDragImage: vi.fn(),
  } as DataTransfer;
}

function gridSlot(container: HTMLElement, slot: number): HTMLElement {
  const element = container.querySelector<HTMLElement>(`.plot-grid > [data-slot="${slot}"]`);
  if (!element) throw new Error(`Plot grid slot ${slot} not found`);
  return element;
}

describe("plot workspace model", () => {
  it("defines every required layout capacity", () => {
    expect(Object.fromEntries(Object.entries(PLOT_LAYOUTS).map(([id, value]) => [id, value.capacity]))).toEqual({
      "1x1": 1,
      "1x2": 2,
      "2x2": 4,
      "2x3": 6,
      "3x3": 9,
    });
    expect(Object.fromEntries(Object.entries(PLOT_LAYOUTS).map(([id, value]) => [id, value.rowHeight]))).toEqual({
      "1x1": 560,
      "1x2": 420,
      "2x2": 360,
      "2x3": 320,
      "3x3": 300,
    });
  });

  it("creates the Roll Hold preset independently of layout", () => {
    const plots = createPresetPlots(ROLL_HOLD_PRESET);
    expect(plots.map((plot) => plot.title)).toEqual(["Roll Angle", "Roll Rate", "Aileron"]);
    expect(signalUnion(plots)).toEqual(["aileron", "commanded_roll", "commanded_roll_rate", "roll", "roll_rate"]);
  });

  it("defines the contract-driven PX4 Course Hold preset", () => {
    const plots = createPresetPlots(COURSE_HOLD_PRESET);
    expect(COURSE_HOLD_PRESET.name).toBe("PX4 Course Hold");
    expect(COURSE_HOLD_PRESET.recommendedLayout).toBe("2x2");
    expect(plots.map((plot) => plot.title)).toEqual([
      "Course Tracking",
      "Course Error",
      "Roll Setpoint Tracking",
      "Ground Speed / Guidance",
    ]);
    expect(plots[0]).toMatchObject({
      angularAware: true,
      acceptanceBandSignal: "course.commanded",
    });
    expect(defaultPresetForScenario("course_hold").id).toBe("px4-course-hold");
  });

  it("defines the contract-driven PX4 Pitch Hold preset", () => {
    const plots = createPresetPlots(PITCH_HOLD_PRESET);
    expect(PITCH_HOLD_PRESET.name).toBe("PX4 Pitch Hold");
    expect(PITCH_HOLD_PRESET.recommendedLayout).toBe("2x2");
    expect(plots.map((plot) => plot.title)).toEqual([
      "Pitch Tracking",
      "Pitch Rate Tracking",
      "Pitch Error / Rate Error",
      "Elevator / Saturation",
    ]);
    expect(plots[0].acceptanceBandSignal).toBe("pitch.commanded");
    expect(defaultPresetForScenario("pitch_hold").id).toBe("px4-pitch-hold");
  });

  it("defines the six-panel contract-driven PX4 TECS preset", () => {
    expect(defaultPresetForScenario("tecs").id).toBe("px4-tecs");
    expect(TECS_PRESET.name).toBe("PX4 TECS");
    expect(TECS_PRESET.recommendedLayout).toBe("2x3");
    expect(TECS_PRESET.plots).toHaveLength(6);
    expect(signalUnion(TECS_PRESET.plots)).toContain("tecs.altitude.target");
    expect(signalUnion(TECS_PRESET.plots)).toContain("tecs.underspeed_active");
  });

  it("maps backend contract signal metadata into the categorized view model", () => {
    const catalog = CONTRACT_SIGNALS.map(normalizeSignalMetadata);
    expect(catalog.map((signal) => signal.id).sort()).toEqual([
      "aileron",
      "commanded_roll",
      "commanded_roll_rate",
      "roll",
      "roll_error",
      "roll_rate",
      "roll_rate_error",
    ]);
    const groups = groupSignalDefinitions(catalog);
    expect(groups.map((group) => group.category)).toEqual(["Aircraft State", "Command", "Control"]);
    expect(catalog.find((signal) => signal.id === "aileron")).toMatchObject({
      category: "Control",
      subcategory: "Surfaces",
      unit: "normalized",
    });
    expect(catalog.find((signal) => signal.id === "commanded_roll")?.description).toBe("Roll demand.");
  });

  it("keeps plot templates separate from workspace presets and validates the registry", () => {
    expect(validatePlotTemplateRegistry(PLOT_TEMPLATES)).toEqual([]);
    expect(PLOT_TEMPLATES.filter((template) => template.category === "Roll Hold").map((template) => template.id)).toEqual([
      "roll-tracking",
      "roll-rate-tracking",
      "roll-error",
      "roll-rate-error",
      "roll-control",
    ]);
    expect(ROLL_HOLD_PRESET.plots.map((plot) => plot.signals.map((signal) => signal.name))).toEqual([
      ["commanded_roll", "roll"],
      ["commanded_roll_rate", "roll_rate"],
      ["aileron"],
    ]);
  });

  it("maps roll_hold to Roll Hold and unknown scenarios to Dynamics", () => {
    expect(defaultPresetForScenario("roll_hold").id).toBe("roll-hold");
    expect(defaultPresetForScenario("formation_flight").id).toBe("dynamics");
  });

  it("unwraps displayed Course degrees and renders the scenario acceptance band", async () => {
    const time = [0, 1, 2, 3];
    const courseTelemetry: SignalResponse = {
      time,
      series: {
        "course.commanded": [10, 10, 10, 10],
        "course.actual": [350, 355, 1, 5],
        "course.error": [20, 15, 9, 5],
        roll_setpoint: [0, 5, 8, 4],
        roll: [0, 2, 6, 5],
        ground_speed: [50, 50, 50, 50],
      },
      units: {
        "course.commanded": "deg",
        "course.actual": "deg",
        "course.error": "deg",
        roll_setpoint: "deg",
        roll: "deg",
        ground_speed: "m/s",
      },
      source_points: 4,
      returned_points: 4,
    };
    const source = {
      key: "course-run",
      loadSignals: vi.fn().mockResolvedValue(courseTelemetry),
      listSignals: vi.fn().mockResolvedValue(
        Object.keys(courseTelemetry.series).map((name) => ({
          name,
          display_name: name.split(".").at(-1)?.replaceAll("_", " ").replace(/\b\w/g, (letter) => letter.toUpperCase()),
          unit: courseTelemetry.units[name],
        })),
      ),
    };
    render(<AnalysisWorkspace
      dataSource={source}
      scenarioType="course_hold"
      acceptanceBandDeg={1}
    />);
    const chart = await screen.findByLabelText("Course Tracking chart");
    const series = JSON.parse(chart.getAttribute("data-series") ?? "[]") as Array<{
      name: string;
      values: number[];
    }>;
    expect(series.find((item) => item.name === "Actual")?.values).toEqual([
      -10, -5, 1, 5,
    ]);
    expect(series.find((item) => item.name === "Commanded + acceptance")?.values)
      .toEqual([11, 11, 11, 11]);
    expect(series.find((item) => item.name === "Commanded - acceptance")?.values)
      .toEqual([9, 9, 9, 9]);
  });

  it("keeps the registry unique and rejects malformed definitions", () => {
    expect(validatePresetRegistry(ANALYSIS_PRESETS)).toEqual([]);
    expect(validatePresetRegistry([ROLL_HOLD_PRESET, ROLL_HOLD_PRESET])).toContain("duplicate preset id: roll-hold");
    expect(validatePresetRegistry([{ ...DYNAMICS_PRESET, id: "bad", plots: [{ id: "empty", title: "", signals: [{ name: "" }] }] }])).toEqual([
      "preset bad plot empty title must not be empty",
      "preset bad plot empty has an empty signal name",
    ]);
  });

  it("calculates required and optional signal availability", () => {
    expect(presetAvailability(ROLL_HOLD_PRESET, ["commanded_roll", "roll"])).toMatchObject({
      available: 2,
      total: 5,
      requiredAvailable: 2,
      requiredTotal: 4,
      usable: true,
    });
  });

  it("updates shared range, cursor, and selection state", () => {
    let state = createTimeline(0, 10);
    state = timelineReducer(state, { type: "set-range", start: 2, end: 7 });
    state = timelineReducer(state, { type: "set-cursor", time: 3.5 });
    state = timelineReducer(state, { type: "set-selection", range: [3, 4] });
    expect(state).toEqual({ visibleStart: 2, visibleEnd: 7, cursorTime: 3.5, selectedRange: [3, 4] });
  });

  it("retries one reduced union for partial telemetry without per-plot requests", async () => {
    const request = vi.spyOn(api, "signals")
      .mockRejectedValueOnce(new ApiError("channels not found: commanded_roll_rate, aileron", 422))
      .mockResolvedValueOnce({
        ...fullTelemetry,
        series: { commanded_roll: fullTelemetry.series.commanded_roll, roll: fullTelemetry.series.roll },
      });
    await loadRunSignalUnion(42, ["commanded_roll", "roll", "commanded_roll_rate", "aileron"]);
    expect(request).toHaveBeenNthCalledWith(1, 42, ["commanded_roll", "roll", "commanded_roll_rate", "aileron"], 4000);
    expect(request).toHaveBeenNthCalledWith(2, 42, ["commanded_roll", "roll"], 4000);
  });

  it("loads each comparison child once and expands measured series by variant", async () => {
    vi.spyOn(api, "availableSignals").mockResolvedValue({
      signals: Object.keys(fullTelemetry.series).map((name) => ({ name, unit: fullTelemetry.units[name] })),
    });
    vi.spyOn(api, "signals")
      .mockResolvedValueOnce(fullTelemetry)
      .mockResolvedValueOnce({
        ...fullTelemetry,
        series: { ...fullTelemetry.series, roll: fullTelemetry.series.roll.map((value) => value * 0.8) },
      });
    const source = createRunComparisonDataSource([
      { runId: 101, label: "Run #101 · baseline" },
      { runId: 102, label: "Run #102 · primary" },
    ]);
    const telemetry = await source.loadSignals(["commanded_roll", "roll"]);
    expect(api.signals).toHaveBeenCalledTimes(2);
    expect(source.resolveSignal?.("commanded_roll", telemetry)).toHaveLength(1);
    expect(source.resolveSignal?.("roll", telemetry).map((series) => series.name)).toEqual([
      "Run #101 · baseline / Roll",
      "Run #102 · primary / Roll",
    ]);
    expect(source.resolveSignal?.("roll", telemetry, "Run #102 · primary")).toHaveLength(1);
  });

  it("keeps both commanded series when independently selected Runs do not match", async () => {
    vi.spyOn(api, "availableSignals").mockResolvedValue({
      signals: Object.keys(fullTelemetry.series).map((name) => ({ name, unit: fullTelemetry.units[name] })),
    });
    vi.spyOn(api, "signals")
      .mockResolvedValueOnce(fullTelemetry)
      .mockResolvedValueOnce({
        ...fullTelemetry,
        series: { ...fullTelemetry.series, commanded_roll: [0, 0, 4, 4, 4, 4] },
      });
    const source = createRunComparisonDataSource([
      { runId: 101, label: "A — Run #101" },
      { runId: 202, label: "B — Run #202" },
    ]);
    const telemetry = await source.loadSignals(["commanded_roll"]);
    expect(source.resolveSignal?.("commanded_roll", telemetry).map((series) => series.name)).toEqual([
      "A — Run #101 / Commanded Roll",
      "B — Run #202 / Commanded Roll",
    ]);
  });
});

describe("AnalysisWorkspace", () => {
  it("renders comparison overlay and side-by-side panels on one shared timeline", async () => {
    vi.spyOn(api, "availableSignals").mockResolvedValue({
      signals: Object.keys(fullTelemetry.series).map((name) => ({ name, unit: fullTelemetry.units[name] })),
    });
    vi.spyOn(api, "signals").mockResolvedValue(fullTelemetry);
    const source = createRunComparisonDataSource([
      { runId: 101, label: "Run #101 · baseline" },
      { runId: 102, label: "Run #102 · primary" },
    ]);
    const onViewChange = vi.fn();
    const { container, rerender } = render(<AnalysisWorkspace
      dataSource={source}
      scenarioType="roll_hold"
      comparisonView="overlay"
      onComparisonViewChange={onViewChange}
    />);
    await waitFor(() => expect(screen.getAllByTestId("synchronized-chart")).toHaveLength(3));
    fireEvent.click(screen.getByRole("button", { name: "Side-by-side" }));
    expect(onViewChange).toHaveBeenCalledWith("side-by-side");
    rerender(<AnalysisWorkspace
      dataSource={source}
      scenarioType="roll_hold"
      comparisonView="side-by-side"
      onComparisonViewChange={onViewChange}
    />);
    await waitFor(() => expect(screen.getAllByTestId("synchronized-chart")).toHaveLength(6));
    expect(container.querySelector(".plot-grid[data-layout='2x3']")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "zoom Run #101 · baseline · Roll Angle" }));
    for (const chart of screen.getAllByTestId("synchronized-chart")) {
      expect(chart).toHaveAttribute("data-start", "2");
      expect(chart).toHaveAttribute("data-end", "4");
    }
  });

  it("renders a 2x2 Roll Hold workspace and fetches the signal union once", async () => {
    const source = dataSource();
    const { container } = render(<AnalysisWorkspace dataSource={source} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    await waitFor(() => expect(source.loadSignals).toHaveBeenCalledTimes(1));
    expect(source.listSignals).toHaveBeenCalledTimes(1);
    expect(source.loadSignals).toHaveBeenCalledWith(["aileron", "commanded_roll", "commanded_roll_rate", "roll", "roll_rate"]);
    expect(container.querySelectorAll(".plot-grid > [data-slot]")).toHaveLength(4);
    expect(screen.getByText("Roll Rate")).toBeInTheDocument();
    expect(screen.getByText("Aileron")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Add Plot" })).toBeInTheDocument();
    const analysisView = screen.getByLabelText("Run analysis");
    const workspace = screen.getByLabelText("Plot workspace");
    const plotArea = container.querySelector<HTMLElement>(".plot-area");
    const timeline = screen.getByLabelText("Shared timeline");
    expect(plotArea).not.toBeNull();
    expect(plotArea).toContainElement(container.querySelector(".plot-grid"));
    expect(plotArea).not.toContainElement(timeline);
    expect(workspace).not.toContainElement(timeline);
    expect(timeline.parentElement).toBe(analysisView);
    expect(timeline.compareDocumentPosition(workspace) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    expect(window.getComputedStyle(workspace).display).toBe("flex");
    expect(window.getComputedStyle(workspace).overflowX).toBe("hidden");
    expect(window.getComputedStyle(plotArea!).overflowX).toBe("hidden");
    expect(window.getComputedStyle(plotArea!).overflowY).toBe("auto");
    expect(screen.getAllByLabelText("Shared timeline")).toHaveLength(1);
  });

  it("starts dragging only from the handle and moves a plot into an empty cell", async () => {
    const source = dataSource();
    const { container } = render(<AnalysisWorkspace dataSource={source} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    await waitFor(() => expect(source.loadSignals).toHaveBeenCalledTimes(1));

    const handle = screen.getByRole("button", { name: "Move Roll Angle" });
    const chart = screen.getByLabelText("Roll Angle chart");
    expect(handle).toHaveAttribute("draggable", "true");
    expect(chart).not.toHaveAttribute("draggable");
    const transfer = dragDataTransfer();

    fireEvent.dragStart(handle, { dataTransfer: transfer });
    expect(gridSlot(container, 0).querySelector(".plot-panel")).toHaveClass("plot-panel-dragging");
    fireEvent.dragOver(gridSlot(container, 3), { dataTransfer: transfer });
    expect(gridSlot(container, 3)).toHaveClass("plot-grid-cell-drop-target");
    fireEvent.drop(gridSlot(container, 3), { dataTransfer: transfer });

    expect(within(gridSlot(container, 3)).getByText("Roll Angle")).toBeInTheDocument();
    expect(within(gridSlot(container, 0)).getByRole("button", { name: "Add Plot" })).toBeInTheDocument();
    expect(screen.getByText("Modified")).toBeInTheDocument();
    expect(source.loadSignals).toHaveBeenCalledTimes(1);
  });

  it("swaps occupied cells and Reset Preset restores the canonical arrangement", async () => {
    const { container } = render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    const transfer = dragDataTransfer();

    fireEvent.dragStart(screen.getByRole("button", { name: "Move Roll Angle" }), { dataTransfer: transfer });
    fireEvent.dragOver(gridSlot(container, 1), { dataTransfer: transfer });
    fireEvent.drop(gridSlot(container, 1), { dataTransfer: transfer });

    expect(within(gridSlot(container, 0)).getByText("Roll Rate")).toBeInTheDocument();
    expect(within(gridSlot(container, 1)).getByText("Roll Angle")).toBeInTheDocument();
    expect(screen.getByText("Modified")).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Reset Preset" }));
    expect(within(gridSlot(container, 0)).getByText("Roll Angle")).toBeInTheDocument();
    expect(within(gridSlot(container, 1)).getByText("Roll Rate")).toBeInTheDocument();
    expect(within(gridSlot(container, 2)).getByText("Aileron")).toBeInTheDocument();
    expect(within(gridSlot(container, 3)).getByRole("button", { name: "Add Plot" })).toBeInTheDocument();
    expect(screen.queryByText("Modified")).not.toBeInTheDocument();
  });

  it("shares dragged layout changes between maximized and normal workspaces", async () => {
    const { container } = render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    fireEvent.click(screen.getByRole("button", { name: "Maximize workspace" }));
    const dialog = await screen.findByRole("dialog", { name: "Analysis workspace" });
    const dialogGrid = within(dialog).getByLabelText("Maximized plot workspace").querySelector<HTMLElement>(".plot-grid");
    expect(dialogGrid).not.toBeNull();
    const transfer = dragDataTransfer();

    fireEvent.dragStart(within(dialog).getByRole("button", { name: "Move Roll Angle" }), { dataTransfer: transfer });
    fireEvent.dragOver(gridSlot(dialogGrid!, 3), { dataTransfer: transfer });
    fireEvent.drop(gridSlot(dialogGrid!, 3), { dataTransfer: transfer });

    const normalGrid = screen.getByLabelText("Plot workspace").querySelector<HTMLElement>(".plot-grid");
    expect(normalGrid).not.toBeNull();
    expect(within(gridSlot(dialogGrid!, 3)).getByText("Roll Angle")).toBeInTheDocument();
    expect(within(gridSlot(normalGrid!, 3)).getByText("Roll Angle")).toBeInTheDocument();
    expect(within(gridSlot(normalGrid!, 0)).getByRole("button", { name: "Add Plot" })).toBeInTheDocument();
    expect(container.querySelectorAll(".plot-panel-dragging")).toHaveLength(0);
  });

  it("fits every layout to the workspace width without horizontal scrolling", async () => {
    const { container } = render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    for (const [layout, definition] of Object.entries(PLOT_LAYOUTS)) {
      fireEvent.click(screen.getByRole("button", { name: `Use ${layout} plot layout` }));
      expect(container.querySelectorAll(".plot-grid > [data-slot]")).toHaveLength(definition.capacity);
      const grid = container.querySelector<HTMLElement>(`.plot-grid[data-layout='${layout}']`);
      expect(grid).not.toBeNull();
      expect(window.getComputedStyle(grid!).width).toBe("100%");
      expect(window.getComputedStyle(grid!).maxWidth).toBe("100%");
      expect(window.getComputedStyle(grid!).minWidth).toBe("0");
      expect(grid!.style.getPropertyValue("--plot-row-height")).toBe(`${definition.rowHeight}px`);
      expect(grid!.style.getPropertyValue("--plot-rows")).toBe(String(definition.rows));
    }
    expect(screen.getByText("Roll Rate")).toBeInTheDocument();
    expect(screen.getByText("Aileron")).toBeInTheDocument();
    expect(screen.getAllByLabelText("Shared timeline")).toHaveLength(1);
    const timeline = screen.getByLabelText("Shared timeline");
    expect(window.getComputedStyle(timeline).maxWidth).toBe("100%");
    expect(window.getComputedStyle(timeline).overflowX).toBe("hidden");
  });

  it("replaces the fixed row height without retaining the previous layout value", async () => {
    const { container } = render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    const grid = () => container.querySelector<HTMLElement>(".plot-grid");

    fireEvent.click(screen.getByRole("button", { name: "Use 1x1 plot layout" }));
    expect(grid()?.style.getPropertyValue("--plot-row-height")).toBe("560px");
    expect(container.querySelectorAll(".plot-grid > [data-slot]")).toHaveLength(1);

    fireEvent.click(screen.getByRole("button", { name: "Use 3x3 plot layout" }));
    expect(grid()?.style.getPropertyValue("--plot-row-height")).toBe("300px");
    expect(grid()?.style.getPropertyValue("--plot-rows")).toBe("3");
    expect(container.querySelectorAll(".plot-grid > [data-slot]")).toHaveLength(9);
    expect(window.getComputedStyle(container.querySelector<HTMLElement>(".plot-area")!).overflowY).toBe("auto");
  });

  it("adds, removes, and opens a plot in a synchronized maximize dialog", async () => {
    const { container } = render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    fireEvent.click(screen.getByRole("button", { name: "Add Plot" }));
    const dialog = await screen.findByRole("dialog", { name: "Add plot" });
    fireEvent.change(within(dialog).getByLabelText("Plot title"), { target: { value: "Custom Roll" } });
    fireEvent.click(within(dialog).getByRole("checkbox", { name: /^Roll .* roll deg$/i }));
    fireEvent.click(within(dialog).getByRole("button", { name: "Add Plot" }));
    expect(screen.getByText("Custom Roll")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Remove Custom Roll" }));
    expect(screen.queryByText("Custom Roll")).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /Reset Y axis/ })).not.toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "timeline cursor" }));
    fireEvent.click(screen.getByRole("button", { name: "timeline selection" }));
    fireEvent.click(screen.getByRole("button", { name: "Maximize Roll Rate" }));
    const maximizeDialog = await screen.findByRole("dialog", { name: "Roll Rate · Expanded plot" });
    expect(screen.getByText("Roll Angle")).toBeInTheDocument();
    expect(screen.getAllByLabelText("Roll Rate chart")).toHaveLength(2);
    expect(within(maximizeDialog).getByLabelText("Roll Rate chart")).toHaveAttribute("data-cursor", "2.5");
    expect(within(maximizeDialog).getByLabelText("Roll Rate chart")).toHaveAttribute("data-selection", "2:3");
    fireEvent.click(within(maximizeDialog).getByRole("button", { name: "zoom Roll Rate" }));
    expect(screen.getByLabelText("Shared timeline")).toHaveAttribute("data-start", "2");
    expect(screen.getByLabelText("Shared timeline")).toHaveAttribute("data-end", "4");
    expect(screen.getAllByLabelText("Roll Rate chart")[0]).toHaveAttribute("data-start", "2");
    fireEvent.click(within(maximizeDialog).getByRole("button", { name: "Close" }));
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Roll Rate · Expanded plot" })).not.toBeInTheDocument());
    expect(screen.getByText("Roll Angle")).toBeInTheDocument();
    expect(container.querySelector(".plot-grid[data-layout='2x2']")).toBeInTheDocument();
    expect(screen.getByLabelText("Shared timeline")).toHaveAttribute("data-start", "2");
  });

  it("maximizes the whole workspace with shared preset, layout, plots, and timeline state", async () => {
    const { container } = render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    expect(screen.getByRole("button", { name: "Maximize workspace" })).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Maximize workspace" }));
    const dialog = await screen.findByRole("dialog", { name: "Analysis workspace" });
    const maximizedWorkspace = within(dialog).getByLabelText("Maximized plot workspace");
    expect(maximizedWorkspace).toBeInTheDocument();
    expect(window.getComputedStyle(dialog).width).toBe("95vw");
    expect(window.getComputedStyle(dialog).height).toBe("94vh");
    expect(within(dialog).getByLabelText("Plot preset")).toHaveTextContent("Roll Hold");

    fireEvent.click(within(dialog).getByRole("button", { name: "Use 3x3 plot layout" }));
    expect(within(dialog).getByLabelText("Maximized plot workspace").querySelector(".plot-grid[data-layout='3x3']")).toBeInTheDocument();
    expect(container.querySelector(".plot-grid[data-layout='3x3']")).toBeInTheDocument();

    fireEvent.click(within(dialog).getByLabelText("Plot preset"));
    const presetMenu = await screen.findByRole("listbox", { name: "Plot preset options" });
    fireEvent.click(within(presetMenu).getByText("Dynamics"));
    await waitFor(() => expect(screen.getAllByLabelText("Plot preset").every((item) => item.textContent?.includes("Dynamics"))).toBe(true));

    fireEvent.click(within(dialog).getByRole("button", { name: "Edit Attitude" }));
    const settings = await screen.findByRole("dialog", { name: "Plot settings" });
    fireEvent.change(within(settings).getByLabelText("Plot title"), { target: { value: "Focused Attitude" } });
    fireEvent.click(within(settings).getByRole("button", { name: "Apply" }));
    expect(screen.getAllByText("Focused Attitude").length).toBeGreaterThanOrEqual(2);

    fireEvent.click(within(dialog).getByRole("button", { name: "zoom Focused Attitude" }));
    expect(screen.getByLabelText("Shared timeline")).toHaveAttribute("data-start", "2");
    expect(within(screen.getByLabelText("Plot workspace")).getByLabelText("Focused Attitude chart")).toHaveAttribute("data-start", "2");

    fireEvent.click(within(dialog).getByRole("button", { name: "Close" }));
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Analysis workspace" })).not.toBeInTheDocument());
    expect(container.querySelector(".plot-grid[data-layout='3x3']")).toBeInTheDocument();
    expect(screen.getByText("Focused Attitude")).toBeInTheDocument();
    expect(screen.getByLabelText("Shared timeline")).toHaveAttribute("data-start", "2");
  });

  it("preloads and applies one Plot Settings dialog for preset plots", async () => {
    const { container } = render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    const editButton = screen.getByRole("button", { name: "Edit Roll Angle" });
    expect(editButton.querySelector(".bp6-icon-edit")).toBeInTheDocument();
    fireEvent.click(editButton);
    const dialog = await screen.findByRole("dialog", { name: "Plot settings" });
    expect(within(dialog).getByLabelText("Plot title")).toHaveValue("Roll Angle");
    expect(within(dialog).getByRole("checkbox", { name: /^Commanded Roll .* commanded_roll deg$/i })).toBeChecked();
    expect(within(dialog).getByRole("checkbox", { name: /^Roll .* roll deg$/i })).toBeChecked();

    fireEvent.change(within(dialog).getByLabelText("Plot title"), { target: { value: "Configured Roll" } });
    fireEvent.click(within(dialog).getByRole("radio", { name: "Manual" }));
    fireEvent.change(within(dialog).getByLabelText("Y-axis minimum"), { target: { value: "-10" } });
    fireEvent.change(within(dialog).getByLabelText("Y-axis maximum"), { target: { value: "10" } });
    fireEvent.click(within(dialog).getByRole("checkbox", { name: "Show legend" }));
    fireEvent.click(within(dialog).getByRole("button", { name: "Apply" }));

    const chart = screen.getByLabelText("Configured Roll chart");
    expect(chart).toHaveAttribute("data-y-min", "-10");
    expect(chart).toHaveAttribute("data-y-max", "10");
    expect(chart).toHaveAttribute("data-show-legend", "false");
    fireEvent.click(screen.getByRole("button", { name: "Use 3x3 plot layout" }));
    expect(container.querySelectorAll(".plot-grid > [data-slot]")).toHaveLength(9);
    expect(screen.getByText("Configured Roll")).toBeInTheDocument();
  });

  it("does not mutate an existing plot when Plot Settings is cancelled", async () => {
    render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    fireEvent.click(screen.getByRole("button", { name: "Edit Roll Angle" }));
    const dialog = await screen.findByRole("dialog", { name: "Plot settings" });
    fireEvent.change(within(dialog).getByLabelText("Plot title"), { target: { value: "Discarded title" } });
    fireEvent.click(within(dialog).getByRole("checkbox", { name: /^Roll .* roll deg$/i }));
    fireEvent.click(within(dialog).getByRole("button", { name: "Cancel" }));
    expect(screen.queryByText("Discarded title")).not.toBeInTheDocument();
    expect(screen.getByText("Roll Angle")).toBeInTheDocument();
    expect(screen.getByLabelText("Roll Angle chart")).toBeInTheDocument();
  });

  it("validates signal selection and manual Y-axis range before creating a plot", async () => {
    render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    fireEvent.click(screen.getByRole("button", { name: "Add Plot" }));
    const dialog = await screen.findByRole("dialog", { name: "Add plot" });
    const add = within(dialog).getByRole("button", { name: "Add Plot" });
    expect(add).toBeDisabled();
    fireEvent.change(within(dialog).getByLabelText("Search signals"), { target: { value: "ail" } });
    expect(within(dialog).queryByRole("checkbox", { name: /^Roll .* roll deg$/i })).not.toBeInTheDocument();
    fireEvent.click(within(dialog).getByRole("checkbox", { name: /^Aileron .* aileron normalized$/i }));
    expect(add).toBeEnabled();
    fireEvent.click(within(dialog).getByRole("radio", { name: "Manual" }));
    expect(add).toBeDisabled();
    fireEvent.change(within(dialog).getByLabelText("Y-axis minimum"), { target: { value: "5" } });
    fireEvent.change(within(dialog).getByLabelText("Y-axis maximum"), { target: { value: "-5" } });
    expect(within(dialog).getByRole("alert")).toHaveTextContent("minimum less than maximum");
    expect(add).toBeDisabled();
    fireEvent.change(within(dialog).getByLabelText("Y-axis maximum"), { target: { value: "10" } });
    expect(add).toBeEnabled();
  });

  it("renders categorized signal rows with separate KaTeX symbol, id, and unit columns", async () => {
    render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    fireEvent.click(screen.getByRole("button", { name: "Add Plot" }));
    const dialog = await screen.findByRole("dialog", { name: "Add plot" });

    expect(within(dialog).getByText("Command")).toBeInTheDocument();
    expect(within(dialog).getByText("Aircraft State")).toBeInTheDocument();
    expect(within(dialog).getByText("Control")).toBeInTheDocument();
    const rollCheckbox = within(dialog).getByRole("checkbox", { name: /^Roll .* roll deg$/i });
    const row = rollCheckbox.closest("label")?.querySelector(".signal-picker-row-content");
    expect(row).not.toBeNull();
    expect(row?.querySelector(".signal-picker-display-name")).toHaveTextContent("Roll");
    expect(row?.querySelector("code")).toHaveTextContent("roll");
    expect(row?.querySelector("small")).toHaveTextContent("deg");
    expect(row?.querySelector(".signal-symbol")).toHaveAttribute("aria-label", "φ");
    expect(row?.querySelector(".signal-symbol .katex")).not.toBeEmptyDOMElement();
    expect(window.getComputedStyle(row as Element).display).toBe("grid");
    expect(within(dialog).getByRole("checkbox", { name: /^Commanded Roll .* commanded_roll deg$/i })
      .closest("label")?.querySelector(".signal-picker-row-content")).toHaveAttribute("title", "Roll demand.");

    fireEvent.change(within(dialog).getByLabelText("Search signals"), { target: { value: "phi_c" } });
    expect(within(dialog).getByRole("checkbox", { name: /^Commanded Roll .* commanded_roll deg$/i })).toBeInTheDocument();
    expect(within(dialog).queryByRole("checkbox", { name: /^Roll .* roll deg$/i })).not.toBeInTheDocument();
  });

  it("adds signals from a categorized plot template and allows custom edits afterward", async () => {
    render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    fireEvent.click(screen.getByRole("button", { name: "Add Plot" }));
    const dialog = await screen.findByRole("dialog", { name: "Add plot" });
    const templatePicker = within(dialog).getByLabelText("Add from plot template");
    expect(within(dialog).getByRole("group", { name: "Roll Hold" })).toBeInTheDocument();

    fireEvent.change(templatePicker, { target: { value: "roll-tracking" } });
    expect(within(dialog).getByLabelText("Plot title")).toHaveValue("Roll Tracking");
    expect(within(dialog).getByRole("checkbox", { name: /^Commanded Roll .* commanded_roll deg$/i })).toBeChecked();
    const roll = within(dialog).getByRole("checkbox", { name: /^Roll .* roll deg$/i });
    expect(roll).toBeChecked();
    fireEvent.click(roll);
    expect(roll).not.toBeChecked();
    fireEvent.click(within(dialog).getByRole("button", { name: "Add Plot" }));

    expect(screen.getByText("Roll Tracking")).toBeInTheDocument();
    expect(screen.getByLabelText("Roll Tracking chart")).toBeInTheDocument();
  });

  it("keeps partial telemetry usable and identifies missing signals", async () => {
    const partial = { ...fullTelemetry, series: { commanded_roll: fullTelemetry.series.commanded_roll, roll: fullTelemetry.series.roll } };
    render(<AnalysisWorkspace dataSource={dataSource(partial)} scenarioType="roll_hold" />);
    expect(await screen.findAllByText(/Required signal unavailable:/)).toHaveLength(1);
    expect(screen.queryByText(/Required signal unavailable:.*aileron/)).not.toBeInTheDocument();
    expect(screen.getByLabelText("Roll Angle chart")).toBeInTheDocument();
  });

  it("automatically selects a scenario preset and falls back safely", async () => {
    const { unmount } = render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    expect(screen.getByLabelText("Plot preset")).toHaveTextContent("Roll Hold");
    unmount();
    render(<AnalysisWorkspace dataSource={{ ...dataSource(), key: "run-43" }} scenarioType="formation_flight" />);
    await screen.findByText("Attitude");
    expect(screen.getByLabelText("Plot preset")).toHaveTextContent("Dynamics");
  });

  it("keeps layout and timeline when switching presets", async () => {
    const { container } = render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await waitFor(() => expect(screen.getAllByTestId("synchronized-chart")).toHaveLength(3));
    fireEvent.click(screen.getByRole("button", { name: "Use 3x3 plot layout" }));
    fireEvent.click(screen.getByRole("button", { name: "zoom Roll Angle" }));
    fireEvent.click(screen.getByRole("button", { name: "cursor Roll Angle" }));
    fireEvent.click(screen.getByLabelText("Plot preset"));
    fireEvent.click(await screen.findByText("Dynamics"));
    await screen.findByText("Attitude");
    expect(container.querySelectorAll(".plot-grid > [data-slot]")).toHaveLength(9);
    for (const chart of screen.getAllByTestId("synchronized-chart")) {
      expect(chart).toHaveAttribute("data-start", "2");
      expect(chart).toHaveAttribute("data-end", "4");
      expect(chart).toHaveAttribute("data-cursor", "3");
    }
  });

  it("renders categorized dark preset menu states without changing selection behavior", async () => {
    render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    fireEvent.click(screen.getByLabelText("Plot preset"));

    expect(await screen.findByText("Recommended")).toBeInTheDocument();
    expect(screen.getByText("General")).toBeInTheDocument();
    const selected = screen.getByRole("option", { name: /Roll Hold/ });
    expect(selected).toHaveAttribute("aria-selected", "true");
    expect(selected.querySelector(".plot-preset-menu-item-selected")).toBeInTheDocument();

    fireEvent.click(screen.getByText("Dynamics"));
    await screen.findByText("Attitude");
    expect(screen.getByLabelText("Plot preset")).toHaveTextContent("Dynamics");
  });

  it("marks unavailable presets disabled while keeping Raw Signals selectable", async () => {
    const unsupportedTelemetry: SignalResponse = {
      ...fullTelemetry,
      series: { unknown_signal: [0, 1, 2, 3, 4, 5] },
      units: { unknown_signal: "raw" },
    };
    render(<AnalysisWorkspace dataSource={dataSource(unsupportedTelemetry)} scenarioType="roll_hold" />);
    await waitFor(() => expect(screen.queryByText("Loading telemetry")).not.toBeInTheDocument());
    fireEvent.click(screen.getByLabelText("Plot preset"));

    const rollHoldOption = await screen.findByRole("option", { name: /Roll Hold/ });
    expect(rollHoldOption.querySelector(".bp6-disabled")).toHaveAttribute("aria-disabled", "true");
    const rawSignalsOption = screen.getByRole("option", { name: "Raw Signals" });
    expect(rawSignalsOption.querySelector(".bp6-disabled")).not.toBeInTheDocument();
  });

  it("marks a changed canonical preset and can reset it", async () => {
    render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await screen.findByText("Roll Angle");
    fireEvent.click(screen.getByRole("button", { name: "Remove Aileron" }));
    expect(screen.getByText("Modified")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Reset Preset" }));
    expect(screen.getByText("Aileron")).toBeInTheDocument();
    expect(screen.queryByText("Modified")).not.toBeInTheDocument();
  });

  it("propagates zoom and cursor changes to every chart", async () => {
    render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await waitFor(() => expect(screen.getAllByTestId("synchronized-chart")).toHaveLength(3));
    fireEvent.click(screen.getByRole("button", { name: "zoom Roll Angle" }));
    for (const chart of screen.getAllByTestId("synchronized-chart")) {
      expect(chart).toHaveAttribute("data-start", "2");
      expect(chart).toHaveAttribute("data-end", "4");
    }
    fireEvent.click(screen.getByRole("button", { name: "cursor Roll Angle" }));
    for (const chart of screen.getAllByTestId("synchronized-chart")) {
      expect(chart).toHaveAttribute("data-cursor", "3");
    }
  });

  it("controls every plot from one shared timeline and resets the full view", async () => {
    render(<AnalysisWorkspace dataSource={dataSource()} scenarioType="roll_hold" />);
    await waitFor(() => expect(screen.getAllByTestId("synchronized-chart")).toHaveLength(3));
    const sharedTimeline = screen.getByLabelText("Shared timeline");
    expect(screen.getAllByLabelText("Shared timeline")).toHaveLength(1);

    fireEvent.click(screen.getByRole("button", { name: "timeline range" }));
    fireEvent.click(screen.getByRole("button", { name: "timeline cursor" }));
    fireEvent.click(screen.getByRole("button", { name: "timeline selection" }));
    expect(sharedTimeline).toHaveAttribute("data-start", "1");
    expect(sharedTimeline).toHaveAttribute("data-end", "4");
    expect(sharedTimeline).toHaveAttribute("data-cursor", "2.5");
    expect(sharedTimeline).toHaveAttribute("data-selection", "2:3");
    for (const chart of screen.getAllByTestId("synchronized-chart")) {
      expect(chart).toHaveAttribute("data-start", "1");
      expect(chart).toHaveAttribute("data-end", "4");
      expect(chart).toHaveAttribute("data-cursor", "2.5");
      expect(chart).toHaveAttribute("data-selection", "2:3");
    }

    fireEvent.click(screen.getByRole("button", { name: "Reset View" }));
    expect(sharedTimeline).toHaveAttribute("data-start", "0");
    expect(sharedTimeline).toHaveAttribute("data-end", "5");
    expect(sharedTimeline).toHaveAttribute("data-cursor", "none");
    expect(sharedTimeline).toHaveAttribute("data-selection", "none");
  });
});
