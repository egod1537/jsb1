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
  parameters: { command_start_sec: 5, settling_band_deg: 0.5, aileron_limit: 0.75 },
  targets: {
    oscillation_count: { value: 2, unit: "cycles", source: "default" },
    residual_oscillation_pp_deg: { value: 1, unit: "deg", source: "default" },
    aileron_saturation_time_s: { value: 0, unit: "s", source: "default" },
  },
  regions: {
    response: { start_sec: 5, end_sec: 20 },
    settling: { start_sec: 5, end_sec: 8.4 },
    steady_state: { start_sec: 15, end_sec: 20 },
  },
  intervals: { aileron_saturation: [{ start_sec: 8, end_sec: 8.2 }] },
  markers: {
    command: { time_sec: 5, value: 5, label: "Command" },
    rise_10: { time_sec: 5.2, value: 0.5, label: "Rise 10%" },
    rise_90: { time_sec: 6.4, value: 4.5, label: "Rise 90%" },
    peak_roll: { time_sec: 7, value: 6, label: "Peak roll" },
    settled: { time_sec: 8.4, value: 5, label: "Settled" },
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
      status: "pass", actual: 0.1, target: 0.5, unit: "deg", target_source: "default",
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
    expect(within(dialog).getByLabelText("Roll Tracking analyzer chart").getAttribute("data-annotations")).toContain("Residual min");
    expect(within(dialog).getByLabelText("Aileron analyzer chart").getAttribute("data-annotations")).toContain("Saturation");

    const body = dialog.querySelector<HTMLElement>(".roll-hold-analyzer-dialog-body")!;
    const content = dialog.querySelector<HTMLElement>(".roll-hold-analyzer-content")!;
    const plots = within(dialog).getByLabelText("Roll Hold plots");
    const charts = dialog.querySelector<HTMLElement>(".roll-hold-analyzer-charts")!;
    expect(window.getComputedStyle(body).display).toBe("flex");
    expect(window.getComputedStyle(body).flexGrow).toBe("1");
    expect(window.getComputedStyle(body).overflow).toBe("hidden");
    expect(window.getComputedStyle(content).flexGrow).toBe("1");
    expect(window.getComputedStyle(content).minHeight).toBe("0");
    expect(window.getComputedStyle(content).overflowY).toBe("auto");
    expect(window.getComputedStyle(plots).flexGrow).toBe("1");
    expect(window.getComputedStyle(charts).gridTemplateRows).toBe("repeat(3, minmax(260px, 1fr))");

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

    fireEvent.click(within(targets).getByRole("button", { name: "Overshoot" }));
    expect(slots()).toHaveLength(3);
    expect(slots()[0]).toHaveAttribute("data-plot-id", "roll_tracking");
    expect(slots()[0]).toHaveAttribute("data-diagnostic-role", "primary");
    expect(annotations("Roll Tracking").points.find((point: { label: string }) => point.label === "Peak")).toMatchObject({ emphasized: true });

    fireEvent.click(within(targets).getByRole("button", { name: "Settling Time" }));
    expect(annotations("Roll Tracking").verticalLines.find((line: { label: string }) => line.label === "Settled")).toMatchObject({ emphasized: true });
    expect(annotations("Roll Tracking").horizontalBands[0]).toMatchObject({ emphasized: true });
    expect(annotations("Roll Tracking").verticalAreas.find((area: { label: string }) => area.label === "Settling region")).toMatchObject({ emphasized: true });

    fireEvent.click(within(targets).getByRole("button", { name: "Oscillation Cycles" }));
    expect(slots()[0]).toHaveAttribute("data-plot-id", "roll_tracking");
    expect(slots()[0]).toHaveAttribute("data-diagnostic-role", "primary");
    expect(slots()[1]).toHaveAttribute("data-plot-id", "roll_rate");
    expect(slots()[1]).toHaveAttribute("data-diagnostic-role", "secondary");

    fireEvent.click(within(targets).getByRole("button", { name: "Saturation Time" }));
    expect(slots()[0]).toHaveAttribute("data-plot-id", "aileron");
    expect(slots()[0]).toHaveClass("is-diagnostic-primary");
    expect(annotations("Aileron").verticalAreas[0]).toMatchObject({ emphasized: true });
    expect(annotations("Aileron").horizontalLines[0]).toMatchObject({ emphasized: true });
    expect(slots()).toHaveLength(3);

    fireEvent.click(within(targets).getByRole("button", { name: "Clear Focus" }));
    expect(slots().map((slot) => slot.dataset.plotId)).toEqual(["roll_tracking", "roll_rate", "aileron"]);
    expect(slots().every((slot) => slot.dataset.diagnosticRole === "none")).toBe(true);
    expect(focusRange).toHaveBeenCalledWith(5, 20);
  });

  it("compares backend-computed primary and baseline results", async () => {
    const baseline = structuredClone(analysis);
    baseline.metrics.overshoot_deg = 0.5;
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
    expect(within(targets).getByRole("columnheader", { name: "Primary" })).toBeInTheDocument();
    expect(within(targets).getByRole("columnheader", { name: "Baseline" })).toBeInTheDocument();
    expect(within(targets).getByRole("columnheader", { name: "Delta" })).toBeInTheDocument();
    expect(within(dialog).getByLabelText("Roll Tracking analyzer chart")).toHaveAttribute(
      "data-series",
      expect.stringContaining("primary / roll"),
    );
    expect(within(dialog).getByLabelText("Roll Tracking analyzer chart")).toHaveAttribute(
      "data-series",
      expect.stringContaining("baseline / roll"),
    );
  });
});
