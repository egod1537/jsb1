import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { MemoryRouter, useLocation } from "react-router-dom";
import { AppShell } from "./AppShell";
import { ConfirmAction } from "./ConfirmAction";
import { DeploymentRevision, metadataMatches } from "./DeploymentRevision";
import { NewRunForm } from "./NewRunForm";
import { StatusTag } from "./StatusTag";

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

function LocationProbe() {
  return <output aria-label="Current path">{useLocation().pathname}</output>;
}

describe("AppShell", () => {
  it("keeps client-side navigation semantics", () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ branch: "dev", commit: "unknown", short_commit: "unknown", built_at: "unknown", hostname: null }),
    }));
    render(
      <MemoryRouter initialEntries={["/runs"]}>
        <AppShell><LocationProbe /></AppShell>
      </MemoryRouter>,
    );

    expect(screen.getByRole("navigation", { name: "Primary navigation" })).toBeInTheDocument();
    fireEvent.click(screen.getByRole("menuitem", { name: "Deployments" }));
    expect(screen.getByLabelText("Current path")).toHaveTextContent("/deployments");
  });
});

describe("DeploymentRevision", () => {
  const info = {
    branch: "impl",
    commit: "8efb0664ab92f2df6155281415fbe33051366868",
    shortCommit: "8efb066",
    builtAt: "2026-08-29T04:42:00Z",
    hostname: "impl-jsb.mangagaki.net",
  };

  it("links the deployed commit and exposes full metadata in a tooltip", async () => {
    render(<DeploymentRevision info={info} />);
    const revision = screen.getByRole("link", { name: "Deployed revision impl @ 8efb066" });

    expect(revision).toHaveAttribute(
      "href",
      "https://github.com/egod1537/jsb1/commit/8efb0664ab92f2df6155281415fbe33051366868",
    );
    expect(revision).toHaveAttribute("target", "_blank");
    fireEvent.mouseEnter(revision);
    expect(await screen.findByText(`Commit: ${info.commit}`)).toBeInTheDocument();
    expect(screen.getByText(`Host: ${info.hostname}`)).toBeInTheDocument();
  });

  it("renders development metadata without a GitHub link", () => {
    render(<DeploymentRevision info={{ branch: "dev", commit: "unknown", shortCommit: "unknown", builtAt: "unknown" }} />);

    expect(screen.getByText("dev")).toBeInTheDocument();
    expect(screen.queryByRole("link")).not.toBeInTheDocument();
  });

  it("detects frontend/backend revision mismatches", () => {
    expect(metadataMatches(info, {
      branch: "impl",
      commit: "9ae3eee902040000000000000000000000000000",
      short_commit: "9ae3eee",
      built_at: info.builtAt,
      hostname: info.hostname,
    })).toBe(false);
  });
});

describe("StatusTag", () => {
  it("renders operational status text", () => {
    render(<StatusTag status="running" />);
    expect(screen.getByText("running")).toBeInTheDocument();
  });
});

describe("ConfirmAction", () => {
  it("requires an explicit destructive confirmation", () => {
    const onConfirm = vi.fn();
    render(
      <ConfirmAction
        isOpen
        title="Stop impl?"
        message="The preview will be unavailable."
        confirmLabel="Stop deployment"
        onCancel={() => undefined}
        onConfirm={onConfirm}
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "Stop deployment" }));
    expect(onConfirm).toHaveBeenCalledOnce();
  });
});

describe("NewRunForm", () => {
  it("selects a JSB0 branch and submits branch-based immutable resolution input", async () => {
    const fetchMock = vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
      const path = String(input);
      const body = path === "/api/scenarios" ? ["roll_hold.yaml"]
        : path === "/api/autopilots" ? ["baseline", "primary"]
          : path === "/api/runtime/repository" ? {
            id: 1, name: "jsb0", remote_url: "", local_path: "jsb0", default_branch: "backend",
            created_at: "", updated_at: "", last_fetched_at: null, current_branch: "backend",
            head_commit: "83ab1f21ca000000", dirty: false, status: "ready",
          }
            : path === "/api/runtime/branches" ? [
              { name: "backend", commit_sha: "1111111111000000", current: true, remote: false },
              { name: "backend", commit_sha: "83ab1f21ca000000", current: false, remote: true },
              { name: "main", commit_sha: "2222222222000000", current: false, remote: true },
            ]
              : path === "/api/runs" && init?.method === "POST" ? {
                id: 42, status: "queued", repository_id: 1, branch: "backend",
                commit_sha: "83ab1f21ca000000", build_id: 7, build_status: "completed", build_reused: true,
              } : [];
      return Promise.resolve({ ok: true, status: 200, json: async () => body } as Response);
    });
    vi.stubGlobal("fetch", fetchMock);
    render(<MemoryRouter><NewRunForm onClose={() => undefined} /></MemoryRouter>);

    expect(screen.getByRole("dialog", { name: "New simulation run" })).toBeInTheDocument();
    expect(await screen.findByLabelText("Scenario")).toBeInTheDocument();
    const autopilot = screen.getByLabelText("Autopilot");
    expect(autopilot).toHaveValue("baseline");
    expect(within(autopilot).getByRole("option", { name: "baseline" })).toBeInTheDocument();
    expect(within(autopilot).getByRole("option", { name: "primary" })).toBeInTheDocument();
    const branch = await screen.findByLabelText("JSB0 Branch");
    expect(branch).toHaveValue("backend");
    expect(screen.getByText("83ab1f21ca00")).toBeInTheDocument();
    expect(screen.queryByLabelText("JSB0 Repository")).not.toBeInTheDocument();
    expect(screen.queryByLabelText("Completed build")).not.toBeInTheDocument();
    expect(screen.queryByLabelText("Commit SHA")).not.toBeInTheDocument();
    expect(screen.queryByText("Register the JSB0 repository before queuing a run.")).not.toBeInTheDocument();

    fireEvent.change(branch, { target: { value: "main" } });
    expect(screen.getByText("222222222200")).toBeInTheDocument();
    fireEvent.change(branch, { target: { value: "backend" } });

    fireEvent.click(screen.getByRole("button", { name: "Run simulation" }));
    await waitFor(() => expect(fetchMock).toHaveBeenCalledWith(
      "/api/runs",
      expect.objectContaining({
        method: "POST",
        body: JSON.stringify({
          scenario: "roll_hold.yaml",
          autopilot: "baseline",
          branch: "backend",
        }),
      }),
    ));
  });

  it("shows a system configuration error without exposing a repository selector", async () => {
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/api/scenarios") {
        return Promise.resolve({ ok: true, status: 200, json: async () => ["roll_hold.yaml"] } as Response);
      }
      if (path === "/api/autopilots") {
        return Promise.resolve({ ok: true, status: 200, json: async () => ["baseline", "primary"] } as Response);
      }
      return Promise.resolve({
        ok: false,
        status: 503,
        json: async () => ({ detail: "JSB0 Runtime repository is not configured" }),
      } as Response);
    }));

    render(<MemoryRouter><NewRunForm onClose={() => undefined} /></MemoryRouter>);

    expect(await screen.findByRole("alert")).toHaveTextContent("JSB0 Runtime repository is not configured.");
    expect(screen.queryByLabelText("JSB0 Repository")).not.toBeInTheDocument();
    expect(screen.getByLabelText("JSB0 Branch")).toBeDisabled();
    expect(screen.getByRole("button", { name: "Run simulation" })).toBeDisabled();
  });
});
