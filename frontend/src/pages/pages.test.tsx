import { cleanup, render, screen, waitFor } from "@testing-library/react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { afterEach, describe, expect, it, vi } from "vitest";
import { RunDetailPage } from "./RunDetailPage";
import { RunsPage } from "./RunsPage";

vi.mock("../components/TimeSeriesChart", () => ({
  TimeSeriesChart: ({ title }: { title: string }) => <div>{title} chart</div>,
}));

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

function response(body: unknown, ok = true): Response {
  return { ok, status: ok ? 200 : 500, json: async () => body } as Response;
}

describe("RunsPage", () => {
  it("renders run rows", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(response([{
      id: 7, status: "completed", commit_sha: "abcdef1234", scenario_name: "roll.yaml",
      autopilot: "primary", created_at: "2026-01-01T00:00:00Z", wall_time_sec: 2.2,
    }])));
    render(<MemoryRouter><RunsPage /></MemoryRouter>);
    expect(screen.getByText("Loading runs")).toBeInTheDocument();
    expect(await screen.findByText("roll.yaml")).toBeInTheDocument();
    expect(screen.getByText("abcdef1")).toBeInTheDocument();
  });

  it("shows API errors", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(response({ detail: "database unavailable" }, false)));
    render(<MemoryRouter><RunsPage /></MemoryRouter>);
    expect(await screen.findByRole("alert")).toHaveTextContent("database unavailable");
  });
});

describe("RunDetailPage", () => {
  it("renders metadata and metrics", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(response({
      run: {
        id: 42, status: "failed", commit_sha: "abc123456", scenario_name: "roll_hold.yaml",
        scenario_path: "scenario/roll_hold.yaml", autopilot: "primary",
        created_at: "2026-01-01T00:00:00Z", started_at: null, finished_at: null,
        exit_code: 1, simulation_time_sec: null, wall_time_sec: 0.2,
        output_directory: "runs/000042", error_message: "runner failed",
      },
      metrics: [{ name: "rms_error_deg", value: 0.17, unit: "deg" }], artifacts: [],
    })));
    render(<MemoryRouter initialEntries={["/runs/42"]}><Routes><Route path="/runs/:id" element={<RunDetailPage />} /></Routes></MemoryRouter>);
    expect(await screen.findByText("Run #42")).toBeInTheDocument();
    expect(screen.getByText("0.170 deg")).toBeInTheDocument();
    expect(screen.getByText("runner failed")).toBeInTheDocument();
  });

  it("keeps the loading state while the API is pending", async () => {
    vi.stubGlobal("fetch", vi.fn(() => new Promise(() => undefined)));
    render(<MemoryRouter initialEntries={["/runs/1"]}><Routes><Route path="/runs/:id" element={<RunDetailPage />} /></Routes></MemoryRouter>);
    await waitFor(() => expect(screen.getByText("Loading run")).toBeInTheDocument());
  });
});
