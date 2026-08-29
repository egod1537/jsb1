import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { afterEach, describe, expect, it, vi } from "vitest";
import { RunDetailPage } from "./RunDetailPage";
import { RunsPage } from "./RunsPage";
import { DeploymentsPage } from "./DeploymentsPage";

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
    expect(screen.getByText("abcdef1234")).toBeInTheDocument();
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

describe("DeploymentsPage", () => {
  it("selects a dense table row and exposes its inspector details", async () => {
    const deployment = (id: number, branch: string, commit: string, hostname: string) => ({
      id,
      repository_id: 1,
      branch,
      commit_sha: commit,
      slug: branch,
      hostname,
      status: "running",
      frontend_port: 8100 + id,
      backend_port: 8200 + id,
      compose_project: `jsb1-${branch}`,
      worktree_path: `/worktrees/${branch}`,
      created_at: "2026-01-01T00:00:00Z",
      started_at: "2026-01-01T00:01:00Z",
      stopped_at: null,
      updated_at: "2026-01-01T00:01:00Z",
      error_message: null,
    });
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/api/deployments") return Promise.resolve(response([
        deployment(1, "impl", "aaaaaaaaaa000000", "impl-jsb.mangagaki.net"),
        deployment(2, "backend", "bbbbbbbbbb000000", "backend-jsb.mangagaki.net"),
      ]));
      if (path === "/api/repositories") return Promise.resolve(response([{
        id: 1, name: "jsb1", remote_url: "", local_path: "", default_branch: "main",
        created_at: "", updated_at: "", last_fetched_at: null, current_branch: "impl",
        head_commit: "aaaaaaaaaa000000", dirty: false, status: "ready",
      }]));
      return Promise.resolve(response([]));
    }));

    render(<MemoryRouter><DeploymentsPage /></MemoryRouter>);
    const inspector = await screen.findByRole("complementary", { name: "Deployment inspector" });
    expect(await within(inspector).findByText("impl")).toBeInTheDocument();

    fireEvent.click(screen.getByRole("row", { name: /backend bbbbbbbbbb/ }));
    expect(within(inspector).getByText("backend")).toBeInTheDocument();
    expect(within(inspector).getByText("backend-jsb.mangagaki.net")).toBeInTheDocument();
  });
});
