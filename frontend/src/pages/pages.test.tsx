import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { hideContextMenu } from "@blueprintjs/core";
import { MemoryRouter, Route, Routes, useLocation } from "react-router-dom";
import { afterEach, describe, expect, it, vi } from "vitest";
import { RunDetailPage } from "./RunDetailPage";
import { RunsPage } from "./RunsPage";
import { SettingsPage } from "./SettingsPage";
import { BuildsPage } from "./BuildsPage";
import { RunComparePage } from "./RunComparePage";

vi.mock("../components/TimeSeriesChart", () => ({
  TimeSeriesChart: ({ title }: { title: string }) => <div>{title} chart</div>,
}));

vi.mock("../features/plots/SharedTimeline", () => ({
  SharedTimeline: () => <footer className="shared-timeline" aria-label="Shared timeline" />,
}));

vi.mock("../components/toast", () => ({
  showOperationToast: vi.fn(),
  showSuccess: vi.fn(),
}));

afterEach(() => {
  hideContextMenu();
  cleanup();
  vi.unstubAllGlobals();
});

function response(body: unknown, ok = true): Response {
  return { ok, status: ok ? 200 : 500, json: async () => body } as Response;
}

function LocationProbe() {
  const location = useLocation();
  return <output aria-label="Current location">{location.pathname}{location.search}</output>;
}

function rollHoldAnalysis() {
  return {
    analyzer: "roll_hold",
    metrics: {
      rise_time_s: 1.2, settling_time_s: 3.4, overshoot_deg: 0.2,
      steady_state_error_deg: 0.1, rms_tracking_error_deg: 0.3,
      peak_roll_rate_deg_s: 4.5, oscillation_count: 1,
      residual_oscillation_pp_deg: 0.2,
      dominant_oscillation_period_s: 2, dominant_oscillation_frequency_hz: 0.5,
      peak_aileron: 0.4, rms_aileron: 0.2, aileron_saturation_detected: false,
      aileron_saturation_time_s: null, aileron_saturation_fraction: null,
    },
    metric_units: {
      rise_time_s: "s", settling_time_s: "s", overshoot_deg: "deg",
      steady_state_error_deg: "deg", rms_tracking_error_deg: "deg",
      peak_roll_rate_deg_s: "deg/s", oscillation_count: "cycles",
      residual_oscillation_pp_deg: "deg",
      dominant_oscillation_period_s: "s", dominant_oscillation_frequency_hz: "Hz",
      peak_aileron: "normalized", rms_aileron: "normalized", aileron_saturation_detected: "boolean",
      aileron_saturation_time_s: "s", aileron_saturation_fraction: "fraction",
    },
    parameters: { command_start_sec: 5, settling_band_deg: 0.1 },
    targets: { settling_time_s: { value: 10, unit: "s", source: "default" } },
    regions: { response: { start_sec: 5, end_sec: 10 } },
    intervals: {},
    markers: { command: { time_sec: 5, value: 5, label: "Command" } },
    checks: [{
      id: "settling_time", label: "Settling time", category: "tracking", status: "pass",
      actual: 3.4, target: 10, unit: "s", target_source: "default",
      message: "Within target.", start_sec: 5, end_sec: 10,
    }],
    assessment: [{ code: "within_limits", severity: "success", message: "No anomalies detected.", start_sec: null, end_sec: null }],
    missing_signals: [],
  };
}

describe("RunsPage", () => {
  it("renders run rows", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(response([{
      id: 7, status: "completed", commit_sha: "abcdef1234", scenario_name: "roll.yaml",
      autopilot: "primary", execution_variant: "primary", comparison_id: null, created_at: "2026-01-01T00:00:00Z", wall_time_sec: 2.2,
    }])));
    render(<MemoryRouter><RunsPage /></MemoryRouter>);
    expect(screen.getByText("Loading runs")).toBeInTheDocument();
    expect(await screen.findByText("roll.yaml")).toBeInTheDocument();
    expect(screen.getByText("abcdef1234")).toBeInTheDocument();
  });

  it("disables Select all when there are no Runs", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(response([])));
    render(<MemoryRouter><RunsPage /></MemoryRouter>);

    expect(await screen.findByText("No runs yet. Queue the first one.")).toBeInTheDocument();
    expect(screen.getByRole("checkbox", { name: "Select all Runs" })).toBeDisabled();
  });

  it("selects any number of Runs while enabling comparison for exactly two", async () => {
    const runs = [17, 16, 15].map((id) => ({
      id, status: "completed", commit_sha: `commit-${id}`, scenario_name: `run-${id}.yaml`,
      repository_id: 1, repository_name: "jsb0", branch: "impl", build_id: id, build_branch: "impl",
      autopilot: id === 17 ? "primary" : "baseline", execution_variant: id === 17 ? "primary" : "baseline",
      comparison_id: null, created_at: "2026-01-01T00:00:00Z", wall_time_sec: 1,
    }));
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(response(runs)));
    render(<MemoryRouter initialEntries={["/runs"]}><Routes>
      <Route path="/runs" element={<><RunsPage /><LocationProbe /></>} />
      <Route path="/runs/compare" element={<LocationProbe />} />
      <Route path="/runs/:id" element={<div>Run detail route</div>} />
    </Routes></MemoryRouter>);

    const compare = await screen.findByRole("button", { name: "Compare" });
    const selectAll = screen.getByRole("checkbox", { name: "Select all Runs" });
    expect(compare).toBeDisabled();
    expect(selectAll).not.toBeChecked();
    expect(screen.queryByRole("button", { name: "Clear selection" })).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole("checkbox", { name: "Select Run #17" }));
    expect(screen.getByText("1 selected")).toBeInTheDocument();
    expect(compare).toBeDisabled();
    expect(selectAll).toBePartiallyChecked();
    expect(screen.getByRole("heading", { name: "Simulation runs" })).toBeInTheDocument();

    fireEvent.click(screen.getByRole("checkbox", { name: "Select Run #16" }));
    expect(screen.getByText("2 selected")).toBeInTheDocument();
    expect(compare).toBeEnabled();
    expect(screen.getByRole("checkbox", { name: "Select Run #15" })).toBeEnabled();

    fireEvent.click(screen.getByRole("checkbox", { name: "Select Run #15" }));
    expect(screen.getByText("3 selected")).toBeInTheDocument();
    expect(compare).toBeDisabled();
    expect(selectAll).toBeChecked();

    fireEvent.click(selectAll);
    expect(screen.getByText("0 selected")).toBeInTheDocument();
    expect(compare).toBeDisabled();
    expect(selectAll).not.toBeChecked();

    fireEvent.click(selectAll);
    expect(screen.getByText("3 selected")).toBeInTheDocument();
    expect(selectAll).toBeChecked();
    fireEvent.click(screen.getByRole("checkbox", { name: "Select Run #15" }));
    expect(screen.getByText("2 selected")).toBeInTheDocument();
    expect(selectAll).toBePartiallyChecked();
    expect(compare).toBeEnabled();
    fireEvent.click(compare);
    expect(screen.getByLabelText("Current location")).toHaveTextContent("/runs/compare?a=17&b=16");
  });

  it("opens Run Detail from a row click while keeping interactive cells independent", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(response([{
      id: 21, status: "completed", commit_sha: "abc", scenario_name: "row-open.yaml",
      repository_id: 1, repository_name: "jsb0", branch: "impl", build_id: 1, build_branch: "impl",
      autopilot: "primary", execution_variant: "primary", comparison_id: null,
      created_at: "2026-01-01T00:00:00Z", wall_time_sec: 1,
    }])));
    render(<MemoryRouter initialEntries={["/runs"]}><Routes>
      <Route path="/runs" element={<RunsPage />} />
      <Route path="/runs/:id" element={<div>Run detail route</div>} />
    </Routes></MemoryRouter>);

    const row = (await screen.findByText("row-open.yaml")).closest("tr")!;
    fireEvent.click(within(row).getByRole("checkbox", { name: "Select Run #21" }));
    expect(screen.queryByText("Run detail route")).not.toBeInTheDocument();
    fireEvent.click(within(row).getByText("completed"));
    expect(await screen.findByText("Run detail route")).toBeInTheDocument();
  });

  it("shows API errors", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(response({ detail: "database unavailable" }, false)));
    render(<MemoryRouter><RunsPage /></MemoryRouter>);
    expect(await screen.findByRole("alert")).toHaveTextContent("database unavailable");
  });

  it("opens a Run from the row context menu without changing left-click links", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(response([{
      id: 17, status: "completed", commit_sha: "abcdef1234", scenario_name: "context-open.yaml",
      repository_id: null, repository_name: null, branch: null, build_id: null, build_branch: null,
      autopilot: "primary", execution_variant: "primary", comparison_id: null,
      created_at: "2026-01-01T00:00:00Z", wall_time_sec: 1.2,
    }])));
    render(<MemoryRouter initialEntries={["/runs"]}><Routes>
      <Route path="/runs" element={<RunsPage />} />
      <Route path="/runs/:id" element={<div>Opened run route</div>} />
    </Routes></MemoryRouter>);
    const scenario = await screen.findByText("context-open.yaml");
    const row = scenario.closest("tr");
    expect(row).not.toBeNull();

    fireEvent.contextMenu(row!, { pageX: 32, pageY: 48 });
    const menu = await screen.findByRole("menu");
    fireEvent.click(within(menu).getByRole("menuitem", { name: "Open Run" }));
    expect(await screen.findByText("Opened run route")).toBeInTheDocument();
  });

  it("confirms terminal Run deletion and disables deletion for active rows", async () => {
    let runs = [
      {
        id: 17, status: "completed", commit_sha: "aaa", scenario_name: "delete-me.yaml",
        repository_id: null, repository_name: null, branch: null, build_id: null, build_branch: null,
        autopilot: "primary", execution_variant: "primary", comparison_id: null,
        created_at: "2026-01-01T00:00:00Z", wall_time_sec: 1,
      },
      {
        id: 18, status: "running", commit_sha: "bbb", scenario_name: "running.yaml",
        repository_id: null, repository_name: null, branch: null, build_id: null, build_branch: null,
        autopilot: "primary", execution_variant: "primary", comparison_id: null,
        created_at: "2026-01-01T00:00:00Z", wall_time_sec: null,
      },
      {
        id: 19, status: "queued", commit_sha: "ccc", scenario_name: "queued.yaml",
        repository_id: null, repository_name: null, branch: null, build_id: null, build_branch: null,
        autopilot: "primary", execution_variant: "primary", comparison_id: null,
        created_at: "2026-01-01T00:00:00Z", wall_time_sec: null,
      },
    ];
    const fetchMock = vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
      if (init?.method === "DELETE") {
        const id = Number(String(input).split("/").at(-1));
        runs = runs.filter((run) => run.id !== id);
        return Promise.resolve(response({}));
      }
      return Promise.resolve(response(runs));
    });
    vi.stubGlobal("fetch", fetchMock);
    render(<MemoryRouter><RunsPage /></MemoryRouter>);
    const completedRow = (await screen.findByText("delete-me.yaml")).closest("tr")!;

    fireEvent.contextMenu(completedRow, { pageX: 32, pageY: 48 });
    fireEvent.click(within(await screen.findByRole("menu")).getByRole("menuitem", { name: "Delete Run" }));
    let dialog = await screen.findByRole("dialog", { name: "Delete Run #17?" });
    expect(within(dialog).getByText("delete-me.yaml")).toBeInTheDocument();
    expect(within(dialog).getByText("This will delete the run record and its stored artifacts.")).toBeInTheDocument();
    fireEvent.click(within(dialog).getByRole("button", { name: "Cancel" }));
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Delete Run #17?" })).not.toBeInTheDocument());
    expect(fetchMock.mock.calls.filter(([, init]) => init?.method === "DELETE")).toHaveLength(0);
    expect(screen.getByText("delete-me.yaml")).toBeInTheDocument();

    for (const scenarioName of ["running.yaml", "queued.yaml"]) {
      fireEvent.contextMenu(screen.getByText(scenarioName).closest("tr")!, { pageX: 32, pageY: 48 });
      const deleteItem = within(await screen.findByRole("menu")).getByRole("menuitem", { name: "Delete Run" });
      expect(deleteItem).toHaveAttribute("aria-disabled", "true");
      hideContextMenu();
    }

    fireEvent.contextMenu(screen.getByText("delete-me.yaml").closest("tr")!, { pageX: 32, pageY: 48 });
    fireEvent.click(within(await screen.findByRole("menu")).getByRole("menuitem", { name: "Delete Run" }));
    dialog = await screen.findByRole("dialog", { name: "Delete Run #17?" });
    fireEvent.click(within(dialog).getByRole("button", { name: "Delete" }));
    await waitFor(() => expect(screen.queryByText("delete-me.yaml")).not.toBeInTheDocument());
    expect(screen.getByText("running.yaml")).toBeInTheDocument();
    expect(fetchMock.mock.calls.filter(([, init]) => init?.method === "DELETE")).toHaveLength(1);
  });

  it("keeps the Run row and reports the backend error when deletion fails", async () => {
    const run = {
      id: 20, status: "failed", commit_sha: "ddd", scenario_name: "keep-on-error.yaml",
      repository_id: null, repository_name: null, branch: null, build_id: null, build_branch: null,
      autopilot: "primary", execution_variant: "primary", comparison_id: null,
      created_at: "2026-01-01T00:00:00Z", wall_time_sec: 1,
    };
    vi.stubGlobal("fetch", vi.fn((_input: RequestInfo | URL, init?: RequestInit) =>
      Promise.resolve(init?.method === "DELETE"
        ? response({ detail: "artifact cleanup failed" }, false)
        : response([run]))));
    render(<MemoryRouter><RunsPage /></MemoryRouter>);
    const row = (await screen.findByText("keep-on-error.yaml")).closest("tr")!;

    fireEvent.contextMenu(row, { pageX: 32, pageY: 48 });
    fireEvent.click(within(await screen.findByRole("menu")).getByRole("menuitem", { name: "Delete Run" }));
    const dialog = await screen.findByRole("dialog", { name: "Delete Run #20?" });
    fireEvent.click(within(dialog).getByRole("button", { name: "Delete" }));

    expect(await within(dialog).findByText("artifact cleanup failed")).toBeInTheDocument();
    expect(row).toHaveTextContent("keep-on-error.yaml");
  });
});

describe("RunComparePage", () => {
  function detail(id: number, variant: string, overrides: Record<string, unknown> = {}) {
    return {
      run: {
        id, status: "completed", repository_id: 1, repository_name: "jsb0", branch: "impl",
        build_id: 7, build_branch: "impl", commit_sha: "769f3cc000000000", scenario_name: "roll_hold.yaml",
        scenario_type: "roll_hold", scenario_id: "bundled:roll_hold.yaml", scenario_source: "bundled",
        scenario_sha256: "a".repeat(64), scenario_path: "scenario.yaml", autopilot: variant,
        execution_variant: variant, comparison_id: null, created_at: "2026-01-01T00:00:00Z",
        started_at: "2026-01-01T00:00:00Z", finished_at: "2026-01-01T00:00:02Z",
        exit_code: 0, simulation_time_sec: 2, wall_time_sec: 1, output_directory: `runs/${id}`,
        error_message: null, current_stage: null, stages: [], ...overrides,
        controller_parameters: id === 17 ? { FW_RR_P: 0.08, FW_RR_I: 0.1 } : { FW_RR_P: 0.05, FW_RR_I: 0.1 },
        controller_parameter_overrides: id === 17 ? { FW_RR_P: 0.08 } : {},
      },
      metrics: [{ name: "settling_time_sec", value: id === 17 ? 8.2 : 4.1, unit: "s" }],
      artifacts: [], instance: null,
    };
  }

  it("compares two Run resources directly with metrics, analyzer, and shared telemetry", async () => {
    const fetchMock = vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/api/runs/17") return Promise.resolve(response(detail(17, "primary")));
      if (path === "/api/runs/16") return Promise.resolve(response(detail(16, "baseline")));
      if (path.endsWith("/analysis/roll-hold")) return Promise.resolve(response(rollHoldAnalysis()));
      if (path.endsWith("/signals/available")) return Promise.resolve(response({ signals: [
        { name: "commanded_roll", unit: "deg" }, { name: "roll", unit: "deg" },
      ] }));
      if (path.includes("/signals?")) return Promise.resolve(response({
        time: [0, 1, 2], series: { commanded_roll: [0, 5, 5], roll: [0, 4, 5] },
        units: { commanded_roll: "deg", roll: "deg" }, source_points: 3, returned_points: 3,
      }));
      return Promise.resolve(response([]));
    });
    vi.stubGlobal("fetch", fetchMock);
    render(<MemoryRouter initialEntries={["/runs/compare?a=17&b=16"]}><Routes>
      <Route path="/runs/compare" element={<RunComparePage />} />
    </Routes></MemoryRouter>);

    expect(await screen.findByRole("heading", { name: "Compare runs" })).toBeInTheDocument();
    expect(screen.getByText(/A — Run #17 · 769f3cc000/)).toBeInTheDocument();
    expect(screen.getByText(/B — Run #16 · 769f3cc000/)).toBeInTheDocument();
    expect(screen.queryByText("Scenario differs. Telemetry remains aligned by simulation time.")).not.toBeInTheDocument();
    const metrics = screen.getByLabelText("Metrics comparison");
    expect(within(metrics).getByText("8.200 s")).toBeInTheDocument();
    expect(within(metrics).getByText("4.100 s")).toBeInTheDocument();
    expect(within(metrics).getByText("-4.100 s")).toBeInTheDocument();
    expect(await screen.findByLabelText("Shared timeline")).toBeInTheDocument();
    expect(await screen.findByText("Roll Angle")).toBeInTheDocument();
    expect(await screen.findByLabelText("Roll Hold analyzer comparison")).toBeInTheDocument();
    const parameters = screen.getByLabelText("Controller parameter comparison");
    expect(within(parameters).getByText("FW_RR_P")).toBeInTheDocument();
    expect(within(parameters).getByText("-0.03")).toBeInTheDocument();
    expect(fetchMock.mock.calls.some(([input]) => String(input).includes("/api/comparisons"))).toBe(false);
  });

  it("allows different scenarios and commits while showing explicit warnings", async () => {
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/api/runs/17") return Promise.resolve(response(detail(17, "primary")));
      if (path === "/api/runs/16") return Promise.resolve(response(detail(16, "baseline", {
        status: "failed", scenario_name: "altitude_hold.yaml", scenario_type: "altitude_hold",
        scenario_id: "bundled:altitude.yaml", scenario_sha256: "b".repeat(64), commit_sha: "different00000000",
      })));
      return Promise.resolve(response([]));
    }));
    render(<MemoryRouter initialEntries={["/runs/compare?a=17&b=16"]}><Routes>
      <Route path="/runs/compare" element={<RunComparePage />} />
    </Routes></MemoryRouter>);

    expect(await screen.findByText("Scenario differs. Telemetry remains aligned by simulation time.")).toBeInTheDocument();
    expect(screen.getByText("Runtime commit differs. Results represent different immutable JSB0 revisions.")).toBeInTheDocument();
    expect(screen.getAllByText("DIFF").length).toBeGreaterThanOrEqual(2);
    expect(screen.getByText("Both Runs must be completed before telemetry can be overlaid.")).toBeInTheDocument();
  });
});

describe("RunDetailPage", () => {
  it("shows compact Runtime/Build provenance and opens the existing build pipeline in a dialog", async () => {
    const fetchMock = vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/api/builds/9") return Promise.resolve(response({
        id: 9, repository_id: 1, repository_name: "jsb0", commit_sha: "abcdef1234567890",
        branch: "impl", status: "completed", build_dir: "/builds/9",
        executable_path: "/builds/9/jsb-sim-runner", stdout_path: "/builds/9/stdout.log",
        stderr_path: "/builds/9/stderr.log", created_at: "2026-01-01T00:00:00Z",
        started_at: "2026-01-01T00:00:01Z", completed_at: "2026-01-01T00:00:05Z",
        error_message: null, reused: true, current_stage: null,
        stages: [
          { id: "configure", label: "Configure", status: "success", started_at: "2026-01-01T00:00:01Z", finished_at: "2026-01-01T00:00:02Z", duration_sec: 1, message: null, error: null },
          { id: "compile", label: "Compile", status: "success", started_at: "2026-01-01T00:00:02Z", finished_at: "2026-01-01T00:00:05Z", duration_sec: 3, message: null, error: null },
        ],
      }));
      if (path.startsWith("/api/runtime/parameters")) return Promise.resolve(response({
        branch: "impl", commit_sha: "abcdef1234567890", source: "jsb1_px4_roll_hold_adapter", transport: "--parameters",
        parameters: [{ id: "FW_RR_P", display_name: "Roll Rate P", unit: "%/rad/s", default_value: 0.05, variants: ["baseline"] }],
      }));
      return Promise.resolve(response({
        run: {
          id: 52, status: "failed", repository_id: 1, repository_name: "jsb0",
          branch: "impl", build_id: 9, build_branch: "impl", commit_sha: "abcdef1234567890",
          scenario_name: "roll_hold.yaml", scenario_path: "scenario/roll_hold.yaml",
          scenario_type: "roll_hold", autopilot: "primary", execution_variant: "primary",
          comparison_id: null, created_at: "2026-01-01T00:00:00Z", started_at: null,
          finished_at: null, exit_code: 1, simulation_time_sec: null, wall_time_sec: 0.2,
          output_directory: "runs/000052", error_message: null, current_stage: null,
          stages: [{
            id: "resolve_build", label: "Resolve Build", status: "success",
            started_at: "2026-01-01T00:00:00Z", finished_at: "2026-01-01T00:00:00Z",
            duration_sec: 0, message: "Build #9 reused", error: null,
          }],
          controller_parameters: { FW_RR_P: 0.08 },
          controller_parameter_overrides: { FW_RR_P: 0.08 },
        },
        metrics: [], artifacts: [],
      }));
    });
    vi.stubGlobal("fetch", fetchMock);
    render(<MemoryRouter initialEntries={["/runs/52"]}><Routes><Route path="/runs/:id" element={<RunDetailPage />} /></Routes></MemoryRouter>);

    const summary = await screen.findByLabelText("Run summary");
    expect(within(summary).getByText("impl @ abcdef1234")).toBeInTheDocument();
    expect(within(summary).getByText("#9")).toBeInTheDocument();
    expect(within(summary).getByText("REUSED")).toBeInTheDocument();
    expect(await within(summary).findByText("completed")).toBeInTheDocument();
    const scenarioAction = within(summary).getByRole("button", { name: "View Scenario Snapshot" });
    const buildAction = within(summary).getByRole("button", { name: "View Build" });
    const parameterAction = within(summary).getByRole("button", { name: "View Controller Parameters" });
    for (const action of [scenarioAction, buildAction, parameterAction]) {
      expect(action).toHaveTextContent("");
      expect(action.querySelector(".bp6-icon-eye-open")).toBeInTheDocument();
    }

    fireEvent.mouseEnter(parameterAction);
    expect(await screen.findByText("View Controller Parameters")).toBeInTheDocument();
    fireEvent.mouseLeave(parameterAction);
    fireEvent.focus(buildAction);
    expect(await screen.findByText("View Build")).toBeInTheDocument();

    fireEvent.click(parameterAction);
    const parameterDialog = await screen.findByRole("dialog", { name: "Run #52 · Controller Parameters" });
    expect(within(parameterDialog).getByText("Roll Rate P")).toBeInTheDocument();
    expect(within(parameterDialog).getByText("0.08")).toBeInTheDocument();
    fireEvent.click(within(parameterDialog).getByRole("button", { name: "Close" }));

    fireEvent.click(buildAction);
    const dialog = await screen.findByRole("dialog", { name: "Build #9" });
    expect(within(dialog).getByText("abcdef1234567890")).toBeInTheDocument();
    expect(within(dialog).getByLabelText("Build pipeline")).toBeInTheDocument();
    expect(within(dialog).getByText("/builds/9/jsb-sim-runner")).toBeInTheDocument();
    expect(within(dialog).getByRole("button", { name: "View stdout log" })).toHaveAttribute("href", "/api/builds/9/logs/stdout");
    expect(within(dialog).getByRole("button", { name: "View stderr log" })).toHaveAttribute("href", "/api/builds/9/logs/stderr");
  });

  it("renders metadata and metrics", async () => {
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => Promise.resolve(response(String(input).endsWith("/scenario") ? {
      id: "run_snapshot:42", source: "run_snapshot", path: "runs/000042/scenario.yaml",
      name: "roll_hold.yaml", scenario_type: "roll_hold", schema_version: 1,
      sha256: "a".repeat(64), updated_at: null,
      validation: { valid: null, runtime_branch: null, runtime_commit: "abc123456", errors: [] },
      definition: { scenario_type: "roll_hold", name: "roll_hold.yaml" }, raw_yaml: "name: roll_hold.yaml\n",
      provenance: { authority: "frozen run snapshot", expected_sha256: "a".repeat(64), actual_sha256: "a".repeat(64), integrity: "verified" },
    } : {
      run: {
        id: 42, status: "failed", commit_sha: "abc123456", scenario_name: "roll_hold.yaml",
        scenario_path: "scenario/roll_hold.yaml", autopilot: "primary", execution_variant: "primary", comparison_id: null,
        created_at: "2026-01-01T00:00:00Z", started_at: null, finished_at: null,
        exit_code: 1, simulation_time_sec: null, wall_time_sec: 0.2,
        output_directory: "runs/000042", error_message: "runner failed",
        current_stage: "launch_runner",
        stages: [
          { id: "launch_runner", label: "Launch Runner", status: "failed", started_at: "2026-01-01T00:00:00Z", finished_at: "2026-01-01T00:00:01Z", duration_sec: 1, message: null, error: "runner failed" },
          { id: "complete", label: "Complete", status: "skipped", started_at: null, finished_at: "2026-01-01T00:00:01Z", duration_sec: null, message: "Not reached", error: null },
        ],
      },
      metrics: [{ name: "rms_error_deg", value: 0.17, unit: "deg" }], artifacts: [],
    }))));
    render(<MemoryRouter initialEntries={["/runs/42"]}><Routes><Route path="/runs/:id" element={<RunDetailPage />} /></Routes></MemoryRouter>);
    expect(await screen.findByText("Run #42")).toBeInTheDocument();
    expect(screen.getByText("0.170 deg")).toBeInTheDocument();
    expect(screen.getByText("runner failed")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Open artifacts (0)" })).toBeDisabled();
    expect(document.querySelector(".artifact-panel")).not.toBeInTheDocument();
    const executeGroup = screen.getByRole("button", { name: "Execute: failed" });
    fireEvent.click(executeGroup);
    expect(screen.getByRole("region", { name: "Execute detailed stages" })).toHaveTextContent("Launch Runner");
    fireEvent.click(screen.getByRole("button", { name: "View Scenario Snapshot" }));
    expect(await screen.findByText("frozen run snapshot")).toBeInTheDocument();
  });

  it("opens run artifacts on demand without rendering the former fixed panel", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(response({
      run: {
        id: 17, status: "failed", commit_sha: "abc123456", scenario_name: "roll_hold.yaml",
        scenario_path: "scenario/roll_hold.yaml", scenario_type: "roll_hold", autopilot: "primary",
        execution_variant: "primary", comparison_id: null, created_at: "2026-01-01T00:00:00Z",
        started_at: null, finished_at: null, exit_code: 1, simulation_time_sec: null,
        wall_time_sec: 0.2, output_directory: "runs/000017", error_message: null,
        current_stage: null, stages: [],
      },
      metrics: [],
      artifacts: [
        { id: 1, run_id: 17, kind: "scenario", filename: "scenario.yaml", download_url: "/api/runs/17/artifacts/scenario" },
        { id: 2, run_id: 17, kind: "telemetry", filename: "telemetry.mcap", download_url: "/api/runs/17/artifacts/telemetry" },
      ],
    })));
    render(<MemoryRouter initialEntries={["/runs/17"]}><Routes><Route path="/runs/:id" element={<RunDetailPage />} /></Routes></MemoryRouter>);

    const artifactsButton = await screen.findByRole("button", { name: "Open artifacts (2)" });
    expect(artifactsButton).toHaveTextContent("Artifacts 2");
    expect(document.querySelector(".artifact-panel")).not.toBeInTheDocument();
    expect(screen.queryByRole("dialog", { name: "Artifacts · Run #17" })).not.toBeInTheDocument();

    fireEvent.click(artifactsButton);
    const dialog = await screen.findByRole("dialog", { name: "Artifacts · Run #17" });
    expect(within(dialog).getByText("scenario.yaml")).toBeInTheDocument();
    expect(within(dialog).getByText("SCENARIO")).toBeInTheDocument();
    expect(within(dialog).getByText("telemetry.mcap")).toBeInTheDocument();
    expect(within(dialog).getByText("TELEMETRY")).toBeInTheDocument();
    expect(within(dialog).getByRole("link", { name: /scenario.yaml/i })).toHaveAttribute("href", "/api/runs/17/artifacts/scenario");

    fireEvent.click(within(dialog).getByRole("button", { name: "Close" }));
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Artifacts · Run #17" })).not.toBeInTheDocument());
  });

  it("keeps the loading state while the API is pending", async () => {
    vi.stubGlobal("fetch", vi.fn(() => new Promise(() => undefined)));
    render(<MemoryRouter initialEntries={["/runs/1"]}><Routes><Route path="/runs/:id" element={<RunDetailPage />} /></Routes></MemoryRouter>);
    await waitFor(() => expect(screen.getByText("Loading run")).toBeInTheDocument());
  });

  it("places the run-level timeline between the pipeline and plot workspace", async () => {
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path.endsWith("/analysis/roll-hold")) return Promise.resolve(response(rollHoldAnalysis()));
      if (path.endsWith("/signals/available")) return Promise.resolve(response({
        signals: [
          { name: "commanded_roll", unit: "deg" },
          { name: "roll", unit: "deg" },
        ],
        variants: { baseline: ["commanded_roll", "roll"], primary: ["commanded_roll", "roll"] },
      }));
      if (path.includes("/signals?")) return Promise.resolve(response({
        time: [0, 1, 2],
        series: { commanded_roll: [0, 5, 5], roll: [0, 3, 5] },
        units: { commanded_roll: "deg", roll: "deg" },
        source_points: 3,
        returned_points: 3,
      }));
      return Promise.resolve(response({
        run: {
          id: 42, status: "completed", commit_sha: "abc123456", scenario_name: "roll_hold.yaml",
          scenario_path: "scenario/roll_hold.yaml", scenario_type: "roll_hold", autopilot: "primary",
          execution_variant: "compare", execution_mode: "compare", variants: ["baseline", "primary"], comparison_id: null, created_at: "2026-01-01T00:00:00Z",
          started_at: "2026-01-01T00:00:00Z", finished_at: "2026-01-01T00:00:02Z",
          exit_code: 0, simulation_time_sec: 2, wall_time_sec: 0.2,
          output_directory: "runs/000042", error_message: null, current_stage: null,
          stages: [{
            id: "complete", label: "Complete", status: "success",
            started_at: "2026-01-01T00:00:02Z", finished_at: "2026-01-01T00:00:02Z",
            duration_sec: 0, message: null, error: null,
          }],
        },
        metrics: [],
        artifacts: [],
      }));
    }));

    const { container } = render(<MemoryRouter initialEntries={["/runs/42"]}><Routes><Route path="/runs/:id" element={<RunDetailPage />} /></Routes></MemoryRouter>);

    const pipeline = await screen.findByLabelText("Run pipeline");
    const timeline = await screen.findByLabelText("Shared timeline");
    const workspace = await screen.findByLabelText("Plot workspace");
    expect(pipeline.compareDocumentPosition(timeline) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    expect(timeline.compareDocumentPosition(workspace) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    expect(workspace).not.toContainElement(timeline);
    expect(container.querySelector(".run-analysis-view > .shared-timeline")).toBe(timeline);
    expect(window.getComputedStyle(timeline).width).toBe("100%");
    expect(window.getComputedStyle(timeline).minWidth).toBe("0");
    const view = screen.getByLabelText("Telemetry view");
    const primary = within(view).getByRole("button", { name: "Primary" });
    const baseline = within(view).getByRole("button", { name: "Baseline" });
    const overlay = within(view).getByRole("button", { name: "Overlay" });
    expect(overlay).toHaveClass("bp6-active");
    fireEvent.click(primary);
    expect(primary).toHaveClass("bp6-active");
    fireEvent.click(baseline);
    expect(baseline).toHaveClass("bp6-active");
    fireEvent.click(overlay);
    expect(overlay).toHaveClass("bp6-active");
    expect(await screen.findByLabelText("Roll Hold Analyzer")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Open Roll Hold Analyzer" }));
    expect((await screen.findAllByText("3.400 s")).length).toBeGreaterThan(0);
  });
});

describe("BuildsPage", () => {
  it("shows the selected build pipeline and compact current stage", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(response([{
      id: 9, repository_id: 1, repository_name: "jsb0", commit_sha: "abcdef1234567890",
      branch: "impl", status: "running", build_dir: "/builds/9", executable_path: null,
      stdout_path: "/builds/9/stdout.log", stderr_path: "/builds/9/stderr.log",
      created_at: "2026-01-01T00:00:00Z", started_at: "2026-01-01T00:00:01Z",
      completed_at: null, error_message: null, reused: false, current_stage: "compile",
      stages: [
        { id: "configure", label: "Configure", status: "success", started_at: "2026-01-01T00:00:01Z", finished_at: "2026-01-01T00:00:02Z", duration_sec: 1, message: null, error: null },
        { id: "compile", label: "Compile", status: "running", started_at: "2026-01-01T00:00:02Z", finished_at: null, duration_sec: null, message: null, error: null },
      ],
    }])));

    render(<MemoryRouter initialEntries={["/builds?selected=9"]}><BuildsPage /></MemoryRouter>);

    const buildGroup = await screen.findByRole("button", { name: "Build: running" });
    fireEvent.click(buildGroup);
    expect(screen.getByLabelText("Build pipeline")).toBeInTheDocument();
    expect(screen.getAllByText("Compile").length).toBeGreaterThan(1);
  });
});

describe("SettingsPage", () => {
  it("shows the canonical JSB0 status without registration or deletion controls", async () => {
    const fetchMock = vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
      const path = String(input);
      if (path === "/api/runtime/repository/fetch" && init?.method === "POST") {
        return Promise.resolve(response({ status: "ready" }));
      }
      if (path === "/api/runtime/repository") return Promise.resolve(response({
        id: 7, key: "jsb0", display_name: "egod1537/jsb0",
        remote_url: "https://github.com/egod1537/jsb0.git", local_path: "/runtime/jsb0",
        default_branch: "backend", last_fetched_at: "2026-08-29T00:00:00Z",
        current_branch: "backend", head_commit: "05316110fa950000", dirty: false,
        status: "ready", error: null, configuration_source: "platform",
      }));
      if (path === "/api/runtime/branches") return Promise.resolve(response([
        { name: "backend", commit_sha: "05316110fa950000", current: true, remote: true },
      ]));
      return Promise.resolve(response([]));
    });
    vi.stubGlobal("fetch", fetchMock);

    render(<MemoryRouter><SettingsPage /></MemoryRouter>);

    expect(await screen.findByRole("heading", { name: "Settings" })).toBeInTheDocument();
    expect(await screen.findByText("egod1537/jsb0")).toBeInTheDocument();
    expect(screen.getByText("https://github.com/egod1537/jsb0.git")).toBeInTheDocument();
    expect(screen.getAllByText("05316110fa95")).toHaveLength(2);
    expect(screen.queryByRole("button", { name: /register/i })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /delete/i })).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Fetch" }));
    await waitFor(() => expect(fetchMock).toHaveBeenCalledWith(
      "/api/runtime/repository/fetch",
      expect.objectContaining({ method: "POST" }),
    ));
  });
});
