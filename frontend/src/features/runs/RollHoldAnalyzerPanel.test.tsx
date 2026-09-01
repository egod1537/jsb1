import { cleanup, fireEvent, render, screen, within } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { api } from "../../api/client";
import type { RollHoldAnalysis } from "../../types/api";
import { RollHoldAnalyzerPanel } from "./RollHoldAnalyzerPanel";
import "../../styles.css";

vi.mock("../../components/TimeSeriesChart", () => ({
  TimeSeriesChart: ({ title, annotations, series = [] }: { title: string; annotations?: unknown; series?: Array<{ name: string }> }) => <div
    className="chart"
    aria-label={`${title} analyzer chart`}
    data-annotations={JSON.stringify(annotations ?? {})}
    data-series={series.map((item) => item.name).join(",")}
  />,
}));

const analysis: RollHoldAnalysis = {
  analyzer: "roll_hold",
  metrics: {
    rise_time_s: 1.2,
    settling_time_s: 3.4,
    overshoot_deg: 0.25,
    steady_state_error_deg: 0.1,
    rms_tracking_error_deg: 0.3,
    peak_roll_rate_deg_s: 4.5,
    oscillation_count: 3,
    residual_oscillation_pp_deg: 0.4,
    dominant_oscillation_period_s: 2,
    dominant_oscillation_frequency_hz: 0.5,
    peak_aileron: 0.4,
    rms_aileron: 0.2,
    aileron_saturation_detected: false,
    aileron_saturation_time_s: 0.2,
    aileron_saturation_fraction: 0.01,
  },
  metric_units: {
    rise_time_s: "s",
    settling_time_s: "s",
    overshoot_deg: "deg",
    steady_state_error_deg: "deg",
    rms_tracking_error_deg: "deg",
    peak_roll_rate_deg_s: "deg/s",
    oscillation_count: "cycles",
    residual_oscillation_pp_deg: "deg",
    dominant_oscillation_period_s: "s",
    dominant_oscillation_frequency_hz: "Hz",
    peak_aileron: "normalized",
    rms_aileron: "normalized",
    aileron_saturation_detected: "boolean",
    aileron_saturation_time_s: "s",
    aileron_saturation_fraction: "fraction",
  },
  parameters: { command_start_sec: 5, settling_band_deg: 0.1, aileron_limit: 0.75 },
  targets: {
    oscillation_count: { value: 2, unit: "cycles", source: "default" },
    residual_oscillation_pp_deg: { value: 1, unit: "deg", source: "default" },
    aileron_saturation_time_s: { value: 0, unit: "s", source: "default" },
  },
  regions: {
    response: { start_sec: 5, end_sec: 20 },
    settling: { start_sec: 5, end_sec: 8.4 },
    steady_state: { start_sec: 15, end_sec: 20 },
    steady_state_error: { start_sec: 15, end_sec: 20 },
  },
  intervals: { aileron_saturation: [{ start_sec: 8, end_sec: 8.2 }] },
  markers: {
    command: { time_sec: 5, value: 5, label: "Command" },
    rise_10: { time_sec: 5.2, value: 0.5, label: "Rise 10%" },
    rise_90: { time_sec: 6.4, value: 4.5, label: "Rise 90%" },
    peak_roll: { time_sec: 7, value: 6, label: "Peak roll" },
    settled: { time_sec: 8.4, value: 5, label: "Settled" },
    steady_state_mean: { time_sec: 17.5, value: 4.9, label: "Steady-state mean" },
    residual_min: { time_sec: 17, value: 4.8, label: "Residual min" },
    residual_max: { time_sec: 18, value: 5.2, label: "Residual max" },
    peak_roll_rate: { time_sec: 5.8, value: 4.5, label: "Peak roll rate" },
    peak_aileron: { time_sec: 6, value: 0.8, label: "Peak aileron" },
    aileron_saturation: { time_sec: 8, value: 0.75, label: "Saturation" },
  },
  checks: [
    {
      id: "rise_time", label: "Rise Time", category: "tracking",
      status: "pass", actual: 1.2, target: 5, unit: "s", target_source: "default",
      message: "Within target.", start_sec: 5, end_sec: 20,
    },
    {
      id: "settling_time", label: "Settling Time", category: "tracking",
      status: "pass", actual: 3.4, target: 10, unit: "s", target_source: "default",
      message: "Within target.", start_sec: 5, end_sec: 20,
    },
    {
      id: "overshoot", label: "Overshoot", category: "tracking",
      status: "pass", actual: 0.25, target: 1, unit: "deg", target_source: "default",
      message: "Within target.", start_sec: 5, end_sec: 20,
    },
    {
      id: "steady_state_error", label: "Steady-State Error", category: "tracking",
      status: "pass", actual: 0.1, target: 0.1, unit: "deg", target_source: "default",
      message: "Within target.", start_sec: 15, end_sec: 20,
    },
    {
      id: "rms_tracking_error", label: "RMS Tracking Error", category: "tracking",
      status: "unavailable", actual: 0.3, target: null, unit: "deg", target_source: "unavailable",
      message: "Metric or target unavailable.", start_sec: 5, end_sec: 20,
    },
    {
      id: "oscillation", label: "Oscillation Cycles", category: "dynamics",
      status: "fail", actual: 3, target: 2, unit: "cycles", target_source: "default",
      message: "Target exceeded.", start_sec: 6.2, end_sec: 15.4,
    },
    {
      id: "residual_oscillation", label: "Residual P-P", category: "dynamics",
      status: "pass", actual: 0.4, target: 1, unit: "deg", target_source: "default",
      message: "Within target.", start_sec: 15, end_sec: 20,
    },
    {
      id: "dominant_period", label: "Dominant Period", category: "dynamics",
      status: "unavailable", actual: 2, target: null, unit: "s", target_source: "unavailable",
      message: "Metric or target unavailable.", start_sec: 5, end_sec: 20,
    },
    {
      id: "peak_aileron", label: "Peak Aileron", category: "control",
      status: "warn", actual: 0.8, target: 0.75, unit: "normalized", target_source: "scenario",
      message: "Within 25% above target.", start_sec: 5, end_sec: 20,
    },
    {
      id: "rms_aileron", label: "RMS Aileron", category: "control",
      status: "unavailable", actual: 0.2, target: null, unit: "normalized", target_source: "unavailable",
      message: "Metric or target unavailable.", start_sec: 5, end_sec: 20,
    },
    {
      id: "saturation_time", label: "Saturation Time", category: "control",
      status: "fail", actual: 0.2, target: 0, unit: "s", target_source: "default",
      message: "Non-zero value exceeds the zero target.", start_sec: 5, end_sec: 20,
    },
  ],
  assessment: [{
    code: "long_settling_oscillation",
    severity: "warning",
    message: "Oscillation detected from 6.2 s to 15.4 s.",
    start_sec: 6.2,
    end_sec: 15.4,
  }],
  missing_signals: ["commanded_roll_rate"],
};

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe("RollHoldAnalyzerPanel", () => {
  it("opens the analyzer dialog, renders charts, and focuses a check range", async () => {
    vi.spyOn(api, "rollHoldAnalysis").mockResolvedValue(analysis);
    vi.spyOn(api, "availableSignals").mockResolvedValue({
      signals: [
        { name: "commanded_roll", unit: "deg" },
        { name: "roll", unit: "deg" },
        { name: "roll_rate", unit: "deg/s" },
        { name: "aileron", unit: "normalized" },
      ],
    });
    vi.spyOn(api, "signals").mockResolvedValue({
      time: [0, 5, 10, 20],
      series: {
        commanded_roll: [0, 5, 5, 5],
        roll: [0, 0, 5.5, 5],
        roll_rate: [0, 4, 0, 0],
        aileron: [0, 0.4, 0.1, 0],
      },
      units: {
        commanded_roll: "deg",
        roll: "deg",
        roll_rate: "deg/s",
        aileron: "normalized",
      },
      source_points: 4,
      returned_points: 4,
    });
    const focusRange = vi.fn();
    render(<RollHoldAnalyzerPanel
      runId={42}
      timeline={{ visibleStart: 0, visibleEnd: 20, cursorTime: null, selectedRange: null }}
      onVisibleRangeChange={vi.fn()}
      onCursorTimeChange={vi.fn()}
      onFocusRange={focusRange}
    />);

    fireEvent.click(screen.getByRole("button", { name: "Open Roll Hold Analyzer" }));
    const dialog = await screen.findByRole("dialog", { name: "Roll Hold Analyzer" });
    expect((await within(dialog).findAllByText("3.400 s")).length).toBeGreaterThan(0);
    expect(within(dialog).getByLabelText("Roll Tracking analyzer chart")).toBeInTheDocument();
    expect(within(dialog).getByLabelText("Roll Rate analyzer chart")).toBeInTheDocument();
    expect(within(dialog).getByLabelText("Aileron analyzer chart")).toBeInTheDocument();
    expect(within(dialog).getByText("Missing signals: commanded_roll_rate")).toBeInTheDocument();
    expect(within(dialog).getByLabelText("Target Performance")).toHaveTextContent("diagnostic default");
    expect(within(dialog).getByText("Residual P-P")).toBeInTheDocument();
    expect(within(dialog).queryByLabelText("Roll Hold checks")).not.toBeInTheDocument();
    expect(within(dialog).getByLabelText("Roll Hold assessment")).toBeInTheDocument();
    const targets = within(dialog).getByLabelText("Target Performance");
    expect(within(targets).getByText("Tracking")).toBeInTheDocument();
    expect(within(targets).getByText("Dynamics")).toBeInTheDocument();
    expect(within(targets).getByText("Control")).toBeInTheDocument();
    expect(within(targets).getAllByText("PASS").length).toBeGreaterThan(0);
    expect(within(targets).getAllByText("WARN").length).toBeGreaterThan(0);
    expect(within(targets).getAllByText("FAIL").length).toBeGreaterThan(0);
    expect(within(targets).getAllByText("N/A").length).toBeGreaterThan(0);
    const rollAnnotations = JSON.parse(
      within(dialog).getByLabelText("Roll Tracking analyzer chart").getAttribute("data-annotations") ?? "{}",
    );
    expect(rollAnnotations.points).toEqual(expect.arrayContaining([
      expect.objectContaining({ label: "Residual min" }),
    ]));
    expect(rollAnnotations.horizontalBands).toEqual([
      expect.objectContaining({ minimum: 4.9, maximum: 5.1, label: "commanded ±0.1° band" }),
    ]);
    expect(within(targets).getByRole("button", { name: "Steady-State Error" })).toHaveTextContent("Target 0.100 °");
    expect(within(dialog).getByLabelText("Aileron analyzer chart").getAttribute("data-annotations")).toContain("Saturation");

    const body = dialog.querySelector<HTMLElement>(".roll-hold-analyzer-dialog-body")!;
    const content = dialog.querySelector<HTMLElement>(".roll-hold-analyzer-content")!;
    const summary = within(dialog).getByLabelText("Roll Hold summary");
    const workspace = dialog.querySelector<HTMLElement>(".roll-hold-analyzer-workspace")!;
    const targetNavigator = dialog.querySelector<HTMLElement>(".analyzer-target-navigator")!;
    const main = dialog.querySelector<HTMLElement>(".roll-hold-analyzer-main")!;
    const plots = within(dialog).getByLabelText("Roll Hold plots");
    const plotScroll = dialog.querySelector<HTMLElement>(".analyzer-plots-scroll")!;
    const charts = dialog.querySelector<HTMLElement>(".roll-hold-analyzer-charts")!;
    expect(window.getComputedStyle(dialog).margin).toBe("0px");
    expect(window.getComputedStyle(dialog).overflow).toBe("hidden");
    expect(body).not.toHaveClass("bp6-dialog-body-scroll-container");
    expect(window.getComputedStyle(body).display).toBe("flex");
    expect(window.getComputedStyle(body).flexGrow).toBe("1");
    expect(window.getComputedStyle(body).flexBasis).toBe("0px");
    expect(window.getComputedStyle(body).maxHeight).toBe("none");
    expect(window.getComputedStyle(body).margin).toBe("0px");
    expect(window.getComputedStyle(body).overflow).toBe("hidden");
    expect(window.getComputedStyle(content).flexGrow).toBe("1");
    expect(window.getComputedStyle(content).height).toBe("100%");
    expect(window.getComputedStyle(content).minHeight).toBe("0");
    expect(window.getComputedStyle(content).overflow).toBe("hidden");
    expect(window.getComputedStyle(workspace).display).toBe("grid");
    expect(window.getComputedStyle(workspace).flexGrow).toBe("1");
    expect(window.getComputedStyle(workspace).height).toBe("100%");
    expect(window.getComputedStyle(workspace).minHeight).toBe("0");
    expect(window.getComputedStyle(workspace).gridTemplateColumns).toBe("260px minmax(0, 1fr)");
    expect(window.getComputedStyle(workspace).gridTemplateRows).toBe("minmax(0, 1fr)");
    expect(window.getComputedStyle(workspace).overflow).toBe("hidden");
    expect(window.getComputedStyle(targets).height).toBe("100%");
    expect(window.getComputedStyle(targets).overflow).toBe("hidden");
    expect(window.getComputedStyle(targetNavigator).flexGrow).toBe("1");
    expect(window.getComputedStyle(targetNavigator).minHeight).toBe("0");
    expect(window.getComputedStyle(targetNavigator).overflowY).toBe("auto");
    expect(window.getComputedStyle(main).height).toBe("100%");
    expect(window.getComputedStyle(main).minHeight).toBe("0");
    expect(window.getComputedStyle(main).overflow).toBe("hidden");
    expect(window.getComputedStyle(plots).flexGrow).toBe("1");
    expect(window.getComputedStyle(plots).height).toBe("100%");
    expect(window.getComputedStyle(plots).overflow).toBe("hidden");
    expect(window.getComputedStyle(plotScroll).flexGrow).toBe("1");
    expect(window.getComputedStyle(plotScroll).minHeight).toBe("0");
    expect(window.getComputedStyle(plotScroll).overflowY).toBe("auto");
    expect(window.getComputedStyle(charts).gridTemplateRows).toBe("repeat(3, minmax(220px, 1fr))");
    expect(summary.compareDocumentPosition(workspace) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    expect(workspace).toContainElement(targets);
    expect(workspace).toContainElement(plots);

    fireEvent.click(within(dialog).getAllByRole("button", { name: /Oscillation Cycles/ })[0]);
    expect(focusRange).toHaveBeenCalledWith(6.2, 15.4);
  });

  it("uses target metrics as a diagnostic navigator without hiding plots", async () => {
    vi.spyOn(api, "rollHoldAnalysis").mockResolvedValue(analysis);
    vi.spyOn(api, "availableSignals").mockResolvedValue({
      signals: [
        { name: "commanded_roll", unit: "deg" },
        { name: "roll", unit: "deg" },
        { name: "roll_rate", unit: "deg/s" },
        { name: "aileron", unit: "normalized" },
      ],
    });
    vi.spyOn(api, "signals").mockResolvedValue({
      time: [0, 5, 10, 20],
      series: {
        commanded_roll: [0, 5, 5, 5],
        roll: [0, 0, 5.5, 5],
        roll_rate: [0, 4, 0, 0],
        aileron: [0, 0.4, 0.1, 0],
      },
      units: {}, source_points: 4, returned_points: 4,
    });
    const focusRange = vi.fn();
    render(<RollHoldAnalyzerPanel
      runId={42}
      timeline={{ visibleStart: 0, visibleEnd: 20, cursorTime: null, selectedRange: null }}
      onVisibleRangeChange={vi.fn()}
      onCursorTimeChange={vi.fn()}
      onFocusRange={focusRange}
    />);

    fireEvent.click(screen.getByRole("button", { name: "Open Roll Hold Analyzer" }));
    const dialog = await screen.findByRole("dialog", { name: "Roll Hold Analyzer" });
    const targets = within(dialog).getByLabelText("Target Performance");
    const charts = dialog.querySelector<HTMLElement>(".roll-hold-analyzer-charts")!;
    const slots = () => Array.from(charts.querySelectorAll<HTMLElement>(".analyzer-plot-slot"));
    const annotations = (label: string) => JSON.parse(
      within(dialog).getByLabelText(`${label} analyzer chart`).getAttribute("data-annotations") ?? "{}",
    );

    const overshoot = within(targets).getByRole("button", { name: "Overshoot" });
    fireEvent.click(overshoot);
    expect(slots()).toHaveLength(3);
    expect(slots()[0]).toHaveAttribute("data-plot-id", "roll_tracking");
    expect(slots()[0]).toHaveAttribute("data-diagnostic-role", "primary");
    expect(annotations("Roll Tracking").points.find((point: { label: string }) => point.label === "Overshoot point")).toMatchObject({
      time: 7, value: 6, emphasized: true,
    });
    expect(annotations("Roll Tracking").horizontalLines).toEqual([
      expect.objectContaining({ value: 5, label: "Commanded roll", emphasized: true }),
    ]);

    fireEvent.click(overshoot);
    expect(annotations("Roll Tracking").points.find((point: { label: string }) => point.label === "Overshoot point")).toMatchObject({ emphasized: false });
    expect(focusRange).toHaveBeenLastCalledWith(0, 20);

    fireEvent.click(within(targets).getByRole("button", { name: "Rise Time" }));
    expect(annotations("Roll Tracking").points.filter((point: { label: string }) => point.label.startsWith("Rise "))).toEqual([
      expect.objectContaining({ label: "Rise 10%", emphasized: true }),
      expect.objectContaining({ label: "Rise 90%", emphasized: true }),
    ]);
    expect(annotations("Roll Tracking").verticalLines.filter((line: { label: string }) => line.label.startsWith("Rise "))).toEqual([
      expect.objectContaining({ label: "Rise 10%", emphasized: true }),
      expect.objectContaining({ label: "Rise 90%", emphasized: true }),
    ]);
    expect(annotations("Roll Tracking").verticalAreas.find((area: { label: string }) => area.label === "Rise-time interval")).toMatchObject({
      start: 5.2, end: 6.4, emphasized: true,
    });

    fireEvent.click(within(targets).getByRole("button", { name: "Settling Time" }));
    expect(annotations("Roll Tracking").verticalLines.find((line: { label: string }) => line.label === "Settled")).toMatchObject({ emphasized: true });
    expect(annotations("Roll Tracking").horizontalBands[0]).toMatchObject({ emphasized: true });
    expect(annotations("Roll Tracking").verticalAreas.find((area: { label: string }) => area.label === "Settling measurement")).toMatchObject({ emphasized: true });

    fireEvent.click(within(targets).getByRole("button", { name: "Steady-State Error" }));
    expect(annotations("Roll Tracking").points.find((point: { label: string }) => point.label === "Steady-state mean")).toMatchObject({
      time: 17.5, value: 4.9, emphasized: true,
    });
    expect(annotations("Roll Tracking").verticalAreas.find((area: { label: string }) => area.label === "Steady-state error window")).toMatchObject({ emphasized: true });

    fireEvent.click(within(targets).getByRole("button", { name: "RMS Tracking Error" }));
    expect(annotations("Roll Tracking").verticalAreas.find((area: { label: string }) => area.label === "RMS tracking window")).toMatchObject({
      start: 5, end: 20, emphasized: true,
    });

    fireEvent.click(within(targets).getByRole("button", { name: "Oscillation Cycles" }));
    expect(slots()[0]).toHaveAttribute("data-plot-id", "roll_tracking");
    expect(slots()[0]).toHaveAttribute("data-diagnostic-role", "primary");
    expect(slots()[1]).toHaveAttribute("data-plot-id", "roll_rate");
    expect(slots()[1]).toHaveAttribute("data-diagnostic-role", "secondary");
    expect(annotations("Roll Tracking").verticalAreas.find((area: { label: string }) => area.label === "Oscillation analysis window")).toMatchObject({ emphasized: true });
    expect(annotations("Roll Rate").verticalAreas[0]).toMatchObject({ label: "Oscillation analysis window", emphasized: true });

    fireEvent.click(within(targets).getByRole("button", { name: "Residual P-P" }));
    expect(annotations("Roll Tracking").points.filter((point: { label: string }) => point.label.startsWith("Residual "))).toEqual([
      expect.objectContaining({ label: "Residual min", emphasized: true }),
      expect.objectContaining({ label: "Residual max", emphasized: true }),
    ]);
    expect(annotations("Roll Tracking").verticalAreas.find((area: { label: string }) => area.label === "Steady-state region")).toMatchObject({ emphasized: true });

    fireEvent.click(within(targets).getByRole("button", { name: "Peak Aileron" }));
    expect(annotations("Aileron").points.find((point: { label: string }) => point.label === "Peak aileron")).toMatchObject({
      time: 6, value: 0.8, emphasized: true,
    });

    fireEvent.click(within(targets).getByRole("button", { name: "Saturation Time" }));
    expect(slots()[0]).toHaveAttribute("data-plot-id", "aileron");
    expect(slots()[0]).toHaveClass("is-diagnostic-primary");
    expect(annotations("Aileron").verticalAreas[0]).toMatchObject({ emphasized: true });
    expect(annotations("Aileron").horizontalLines[0]).toMatchObject({ emphasized: true });
    expect(slots()).toHaveLength(3);

    fireEvent.click(within(targets).getByRole("button", { name: "Clear Focus" }));
    expect(slots().map((slot) => slot.dataset.plotId)).toEqual(["roll_tracking", "roll_rate", "aileron"]);
    expect(slots().every((slot) => slot.dataset.diagnosticRole === "none")).toBe(true);
    expect(focusRange).toHaveBeenLastCalledWith(0, 20);
  });

  it("compares backend-computed primary and baseline results", async () => {
    const baseline = structuredClone(analysis);
    baseline.metrics.overshoot_deg = 0.5;
    baseline.markers.peak_roll = { time_sec: 8.5, value: 5.5, label: "Peak roll" };
    const baselineOvershoot = baseline.checks.find((check) => check.id === "overshoot")!;
    baselineOvershoot.actual = 0.5;
    vi.spyOn(api, "rollHoldAnalysis").mockResolvedValue({
      variants: { primary: analysis, baseline },
    });
    vi.spyOn(api, "availableSignals").mockResolvedValue({
      signals: [
        { name: "commanded_roll", unit: "deg" },
        { name: "roll", unit: "deg" },
        { name: "roll_rate", unit: "deg/s" },
        { name: "aileron", unit: "normalized" },
      ],
      variants: {
        primary: ["commanded_roll", "roll", "roll_rate", "aileron"],
        baseline: ["commanded_roll", "roll", "roll_rate", "aileron"],
      },
    });
    vi.spyOn(api, "signals").mockImplementation(async (_id, _signals, _points, variant) => ({
      time: [0, 5, 10],
      series: {
        commanded_roll: [0, 5, 5],
        roll: variant === "baseline" ? [0, 3, 4.5] : [0, 4, 5],
        roll_rate: [0, 1, 0],
        aileron: [0, 0.2, 0],
      },
      units: {}, source_points: 3, returned_points: 3,
    }));
    render(<RollHoldAnalyzerPanel
      runId={42}
      timeline={{ visibleStart: 0, visibleEnd: 20, cursorTime: null, selectedRange: null }}
      onVisibleRangeChange={vi.fn()}
      onCursorTimeChange={vi.fn()}
      onFocusRange={vi.fn()}
    />);

    fireEvent.click(screen.getByRole("button", { name: "Open Roll Hold Analyzer" }));
    const dialog = await screen.findByRole("dialog", { name: "Roll Hold Analyzer" });
    const targets = within(dialog).getByLabelText("Target Performance");
    const workspace = dialog.querySelector<HTMLElement>(".roll-hold-analyzer-workspace")!;
    const plots = within(dialog).getByLabelText("Roll Hold plots");
    const view = within(dialog).getByLabelText("Analyzer view");
    const overshoot = within(targets).getByRole("button", { name: "Overshoot" });
    expect(within(overshoot).getByText("Primary")).toBeInTheDocument();
    expect(within(overshoot).getByText("Baseline")).toBeInTheDocument();
    expect(within(overshoot).getByText("Δ")).toBeInTheDocument();
    expect(overshoot).toHaveTextContent("0.250 °");
    expect(overshoot).toHaveTextContent("0.500 °");
    expect(overshoot).toHaveTextContent("-0.250 °");
    expect(workspace).toContainElement(targets);
    expect(workspace).toContainElement(plots);
    expect(within(dialog).getByLabelText("Roll Tracking analyzer chart")).toHaveAttribute(
      "data-series",
      expect.stringContaining("primary / roll"),
    );
    expect(within(dialog).getByLabelText("Roll Tracking analyzer chart")).toHaveAttribute(
      "data-series",
      expect.stringContaining("baseline / roll"),
    );
    fireEvent.click(overshoot);
    const overshootPoint = () => JSON.parse(
      within(dialog).getByLabelText("Roll Tracking analyzer chart").getAttribute("data-annotations") ?? "{}",
    ).points.find((point: { label: string }) => point.label === "Overshoot point");
    expect(overshootPoint()).toMatchObject({ time: 7, value: 6, emphasized: true });
    const rollBand = () => JSON.parse(
      within(dialog).getByLabelText("Roll Tracking analyzer chart").getAttribute("data-annotations") ?? "{}",
    ).horizontalBands[0];
    expect(rollBand()).toMatchObject({ minimum: 4.9, maximum: 5.1, label: "commanded ±0.1° band" });

    fireEvent.click(within(view).getByRole("button", { name: "Primary" }));
    expect(within(targets).getByRole("button", { name: "Overshoot" })).toHaveTextContent("PASS");
    expect(workspace).toContainElement(plots);
    expect(rollBand()).toMatchObject({ minimum: 4.9, maximum: 5.1 });
    expect(overshootPoint()).toMatchObject({ time: 7, value: 6, emphasized: true });

    fireEvent.click(within(view).getByRole("button", { name: "Baseline" }));
    expect(within(targets).getByRole("button", { name: "Overshoot" })).toHaveTextContent("0.500 °");
    expect(workspace).toContainElement(plots);
    expect(rollBand()).toMatchObject({ minimum: 4.9, maximum: 5.1 });
    expect(overshootPoint()).toMatchObject({ time: 8.5, value: 5.5, emphasized: true });

    fireEvent.click(within(view).getByRole("button", { name: "Compare" }));
    expect(within(targets).getByRole("button", { name: "Overshoot" })).toHaveTextContent("Δ");
    expect(workspace).toContainElement(plots);
    expect(rollBand()).toMatchObject({ minimum: 4.9, maximum: 5.1 });
    expect(overshootPoint()).toMatchObject({ time: 7, value: 6, emphasized: true });
  });
});
