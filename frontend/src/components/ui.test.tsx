import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { BrowserRouter, MemoryRouter, useLocation } from "react-router-dom";
import { App } from "../App";
import { AppShell } from "./AppShell";
import { DeploymentRevision, metadataMatches } from "./DeploymentRevision";
import { NewRunForm } from "../features/runs/NewRunForm";
import { StatusTag } from "./StatusTag";

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
  window.history.replaceState({}, "", "/");
});

describe("legacy repository route", () => {
  it("redirects /repositories to the read-only Settings runtime status", async () => {
    window.history.replaceState({}, "", "/repositories");
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/api/version") return Promise.resolve({ ok: true, status: 200, json: async () => ({ branch: "impl", commit: "abc", short_commit: "abc", built_at: "", hostname: null }) } as Response);
      if (path === "/api/runtime/repository") return Promise.resolve({ ok: true, status: 200, json: async () => ({
        id: 1, display_name: "egod1537/jsb0", remote_url: "https://github.com/egod1537/jsb0.git",
        local_path: "/runtime/jsb0", default_branch: "impl", current_branch: "impl",
        head_commit: "abc123", last_fetched_at: null, configuration_source: "platform",
        dirty: false, status: "ready", error: null,
      }) } as Response);
      if (path === "/api/runtime/branches") return Promise.resolve({ ok: true, status: 200, json: async () => [] } as Response);
      return Promise.resolve({ ok: true, status: 200, json: async () => [] } as Response);
    }));

    render(<BrowserRouter><App /></BrowserRouter>);

    expect(await screen.findByRole("heading", { name: "Settings" })).toBeInTheDocument();
    expect(window.location.pathname).toBe("/settings");
    expect(screen.getByRole("menuitem", { name: "Settings" })).toHaveAttribute("aria-current", "page");
  });
});

describe("legacy Builds route", () => {
  it("keeps direct /builds access while omitting Builds from primary navigation", async () => {
    window.history.replaceState({}, "", "/builds");
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/api/version") return Promise.resolve({
        ok: true, status: 200,
        json: async () => ({ branch: "impl", commit: "abc", short_commit: "abc", built_at: "", hostname: null }),
      } as Response);
      return Promise.resolve({ ok: true, status: 200, json: async () => [] } as Response);
    }));

    render(<BrowserRouter><App /></BrowserRouter>);

    expect(await screen.findByRole("heading", { name: "Builds" })).toBeInTheDocument();
    expect(window.location.pathname).toBe("/builds");
    expect(screen.queryByRole("menuitem", { name: "Builds" })).not.toBeInTheDocument();
  });
});

function LocationProbe() {
  return <output aria-label="Current path">{useLocation().pathname}</output>;
}

describe("AppShell", () => {
  it("groups role-based navigation in the required order and keeps active routing", () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ branch: "dev", commit: "unknown", short_commit: "unknown", built_at: "unknown", hostname: null }),
    }));
    render(
      <MemoryRouter initialEntries={["/scenarios"]}>
        <AppShell><LocationProbe /></AppShell>
      </MemoryRouter>,
    );

    const navigation = screen.getByRole("navigation", { name: "Primary navigation" });
    expect(within(navigation).getAllByRole("region").map((section) => section.getAttribute("aria-labelledby"))).toEqual([
      "sidebar-operations",
      "sidebar-assets",
      "sidebar-system",
    ]);
    expect(within(navigation).getAllByRole("menuitem").map((item) => item.textContent)).toEqual([
      "Runs",
      "Scenarios",
      "Settings",
    ]);
    expect(screen.queryByRole("menuitem", { name: "Builds" })).not.toBeInTheDocument();
    expect(screen.queryByRole("menuitem", { name: "Repositories" })).not.toBeInTheDocument();
    expect(screen.queryByRole("menuitem", { name: "Deployments" })).not.toBeInTheDocument();
    expect(screen.getByRole("menuitem", { name: "Scenarios" })).toHaveAttribute("aria-current", "page");

    fireEvent.click(screen.getByRole("menuitem", { name: "Runs" }));
    expect(screen.getByLabelText("Current path")).toHaveTextContent("/runs");
    fireEvent.click(screen.getByRole("menuitem", { name: "Settings" }));
    expect(screen.getByLabelText("Current path")).toHaveTextContent("/settings");
    fireEvent.click(screen.getByRole("menuitem", { name: "Scenarios" }));
    expect(screen.getByLabelText("Current path")).toHaveTextContent("/scenarios");
  });
});

describe("removed Deployments route", () => {
  it("falls back from /deployments to the Runs workflow", async () => {
    window.history.replaceState({}, "", "/deployments");
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/api/version") return Promise.resolve({
        ok: true, status: 200,
        json: async () => ({ branch: "impl", commit: "abc", short_commit: "abc", built_at: "", hostname: null }),
      } as Response);
      return Promise.resolve({ ok: true, status: 200, json: async () => [] } as Response);
    }));

    render(<BrowserRouter><App /></BrowserRouter>);

    expect(await screen.findByRole("heading", { name: "Simulation runs" })).toBeInTheDocument();
    expect(window.location.pathname).toBe("/runs");
    expect(screen.queryByRole("menuitem", { name: "Deployments" })).not.toBeInTheDocument();
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

describe("NewRunForm", () => {
  it("selects a JSB0 branch and submits branch-based immutable resolution input", async () => {
    const fetchMock = vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
      const path = String(input);
      const body = path === "/api/scenarios" ? [{
        id: "roll_hold.yaml", name: "Roll hold", source: "bundled",
        scenario_type: "roll_hold", schema_version: 1,
        controller_parameters: ["FW_RR_P"],
        autopilot: "baseline", valid: true, scenario_sha256: "a".repeat(64),
        validated_runtime_commit: null, last_validated_at: null,
      }]
        : path.startsWith("/api/scenario-catalog/detail?") ? {
            id: "bundled:roll_hold.yaml", source: "bundled", path: "roll_hold.yaml",
            name: "Roll hold", scenario_type: "roll_hold", schema_version: 1,
            sha256: "a".repeat(64), updated_at: null,
            validation: { valid: true, runtime_branch: "main", runtime_commit: "b".repeat(40), errors: [] },
            definition: { scenario_type: "roll_hold", name: "Roll hold" }, raw_yaml: "name: Roll hold\n",
            provenance: { authority: "repository file", expected_sha256: "a".repeat(64), actual_sha256: "a".repeat(64), integrity: "verified" },
          }
        : path === "/api/scenarios/sync/status" ? {
            configured: false, reachable: null, last_sync_at: null,
            last_success_at: null, last_error: null,
          }
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
              : path.startsWith("/api/runtime/variants") ? {
                branch: path.includes("main") ? "main" : "backend",
                commit_sha: path.includes("main") ? "2222222222000000" : "83ab1f21ca000000",
                mode: "compare",
                variants: ["baseline", "primary"],
              }
              : path.startsWith("/api/runtime/parameters") ? {
                branch: path.includes("main") ? "main" : "backend",
                commit_sha: path.includes("main") ? "2222222222000000" : "83ab1f21ca000000",
                source: "jsb1_px4_roll_hold_adapter",
                transport: "--parameters",
                parameters: [{
                  id: "FW_RR_P", display_name: "Roll Rate P", unit: "%/rad/s",
                  default_value: 0.05, minimum: 0, maximum: 10, increment: 0.005,
                  variants: ["baseline"],
                }],
              }
              : path === "/api/runs" && init?.method === "POST" ? {
                id: 42, status: "queued", repository_id: 1, branch: "backend",
                commit_sha: "83ab1f21ca000000", build_id: 7, build_status: "completed", build_reused: true,
              } : [];
      return Promise.resolve({ ok: true, status: 200, json: async () => body } as Response);
    });
    vi.stubGlobal("fetch", fetchMock);
    render(<MemoryRouter><NewRunForm onClose={() => undefined} /></MemoryRouter>);

    expect(screen.getByRole("dialog", { name: "New simulation run" })).toBeInTheDocument();
    const scenarioSelect = await screen.findByLabelText("Scenario");
    const scenarioRow = scenarioSelect.closest(".new-run-scenario-row");
    expect(scenarioRow).not.toBeNull();
    expect(within(scenarioRow as HTMLElement).getByRole("button", { name: "View" })).toBeEnabled();
    const scenarioMetadata = document.querySelector(".scenario-source-summary");
    expect(scenarioMetadata).toHaveTextContent("roll_hold · schema v1 · BUNDLED VALID");
    expect(screen.queryByLabelText("Autopilot")).not.toBeInTheDocument();
    const branch = await screen.findByLabelText("JSB0 Branch");
    expect(branch).toHaveValue("backend");
    await waitFor(() => expect(branch).toHaveDisplayValue("backend · 83ab1f2"));
    expect(screen.getByRole("option", { name: "main · 2222222" })).toBeInTheDocument();
    expect(branch).toHaveAttribute("title", expect.stringContaining("83ab1f21ca000000"));
    expect(screen.queryByLabelText("Resolved revision")).not.toBeInTheDocument();
    expect(screen.queryByText(/Preview only/)).not.toBeInTheDocument();
    expect(screen.queryByLabelText("JSB0 Repository")).not.toBeInTheDocument();
    expect(screen.queryByLabelText("Completed build")).not.toBeInTheDocument();
    expect(screen.queryByLabelText("Commit SHA")).not.toBeInTheDocument();
    expect(screen.queryByText("Register the JSB0 repository before queuing a run.")).not.toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "View" }));
    const scenarioDialog = await screen.findByRole("dialog", { name: "Roll hold" });
    expect(screen.getByText("repository file")).toBeInTheDocument();
    fireEvent.click(within(scenarioDialog).getByRole("button", { name: "Close" }));

    fireEvent.change(branch, { target: { value: "main" } });
    await waitFor(() => expect(branch).toHaveDisplayValue("main · 2222222"));
    fireEvent.change(branch, { target: { value: "backend" } });

    await waitFor(() => expect(screen.getByRole("button", { name: "Run simulation" })).toBeEnabled());
    expect(screen.getByText("1 parameters")).toBeInTheDocument();
    expect(screen.queryByLabelText("FW_RR_P")).not.toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Configure" }));
    let parameterDialog = screen.getByRole("dialog", { name: "Configure Controller Parameters" });
    fireEvent.change(within(parameterDialog).getByLabelText("FW_RR_P"), { target: { value: "0.08" } });
    fireEvent.click(within(parameterDialog).getByRole("button", { name: "Apply" }));
    await waitFor(() => expect(
      screen.queryByRole("dialog", { name: "Configure Controller Parameters" }),
    ).not.toBeInTheDocument());
    fireEvent.click(screen.getByRole("button", { name: "Configure" }));
    parameterDialog = screen.getByRole("dialog", { name: "Configure Controller Parameters" });
    expect(within(parameterDialog).getByLabelText("FW_RR_P")).toHaveValue("0.08");
    fireEvent.change(within(parameterDialog).getByLabelText("FW_RR_P"), { target: { value: "0.12" } });
    fireEvent.click(within(parameterDialog).getByRole("button", { name: "Cancel" }));
    await waitFor(() => expect(
      screen.queryByRole("dialog", { name: "Configure Controller Parameters" }),
    ).not.toBeInTheDocument());
    fireEvent.click(screen.getByRole("button", { name: "Configure" }));
    parameterDialog = screen.getByRole("dialog", { name: "Configure Controller Parameters" });
    expect(within(parameterDialog).getByLabelText("FW_RR_P")).toHaveValue("0.08");
    fireEvent.click(within(parameterDialog).getByRole("button", { name: "Cancel" }));
    fireEvent.click(screen.getByRole("button", { name: "Run simulation" }));
    await waitFor(() => expect(fetchMock).toHaveBeenCalledWith(
      "/api/runs",
      expect.objectContaining({
        method: "POST",
        body: JSON.stringify({
          scenario: "roll_hold.yaml",
          scenario_source: "bundled",
          branch: "backend",
          controller_parameters: { FW_RR_P: 0.08 },
        }),
      }),
    ));
  });

  it("disables Scenario View when there is no selected scenario", async () => {
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      const body = path === "/api/scenarios" ? []
        : path === "/api/runtime/repository" ? { id: 1, default_branch: "impl", status: "ready" }
          : path === "/api/runtime/branches" ? [{ name: "impl", commit_sha: "abc123456789", current: true, remote: true }]
            : path === "/api/scenarios/sync/status" ? { configured: false, reachable: null, last_sync_at: null, last_success_at: null, last_error: null }
              : path.startsWith("/api/runtime/variants") ? { branch: "impl", commit_sha: "abc123456789", mode: "compare", variants: ["baseline", "primary"] }
                : path.startsWith("/api/runtime/parameters") ? { branch: "impl", commit_sha: "abc123456789", parameters: [] }
                  : [];
      return Promise.resolve({ ok: true, status: 200, json: async () => body } as Response);
    }));

    render(<MemoryRouter><NewRunForm onClose={() => undefined} /></MemoryRouter>);

    const scenarioSelect = await screen.findByLabelText("Scenario");
    const scenarioRow = scenarioSelect.closest(".new-run-scenario-row");
    expect(within(scenarioRow as HTMLElement).getByRole("button", { name: "View" })).toBeDisabled();
  });

  it("shows only the Scenario whitelist and reconciles values when Scenario changes", async () => {
    const definitions = [
      { id: "FW_RR_P", display_name: "Roll Rate P", default_value: 0.05, minimum: 0, maximum: 10, variants: ["baseline"] },
      { id: "FW_RR_I", display_name: "Roll Rate I", default_value: 0.1, minimum: 0, maximum: 10, variants: ["baseline"] },
      { id: "FW_RR_D", display_name: "Roll Rate D", default_value: 0, minimum: 0, maximum: 10, variants: ["baseline"] },
    ];
    const fetchMock = vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
      const path = String(input);
      const body = path === "/api/scenarios" ? [
        {
          id: "first.yaml", name: "First", source: "bundled", valid: true,
          controller_parameters: ["FW_RR_P", "FW_RR_I"], scenario_sha256: "a".repeat(64),
        },
        {
          id: "second.yaml", name: "Second", source: "bundled", valid: true,
          controller_parameters: ["FW_RR_I", "FW_RR_D"], scenario_sha256: "b".repeat(64),
        },
      ] : path === "/api/runtime/repository" ? {
        id: 1, default_branch: "impl", status: "ready",
      } : path === "/api/runtime/branches" ? [{
        name: "impl", commit_sha: "abc123456789", current: true, remote: true,
      }] : path === "/api/scenarios/sync/status" ? {
        configured: false, reachable: null, last_sync_at: null, last_success_at: null, last_error: null,
      } : path.startsWith("/api/runtime/variants") ? {
        branch: "impl", commit_sha: "abc123456789", mode: "compare", variants: ["baseline", "primary"],
      } : path.startsWith("/api/runtime/parameters") ? {
        branch: "impl", commit_sha: "abc123456789", parameters: definitions,
      } : path === "/api/runs" && init?.method === "POST" ? {
        id: 88, status: "queued", branch: "impl", commit_sha: "abc123456789",
      } : [];
      return Promise.resolve({ ok: true, status: 200, json: async () => body } as Response);
    });
    vi.stubGlobal("fetch", fetchMock);
    render(<MemoryRouter><NewRunForm onClose={() => undefined} /></MemoryRouter>);

    await waitFor(() => expect(screen.getByRole("button", { name: "Configure" })).toBeEnabled());
    expect(screen.getByText("2 parameters")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Configure" }));
    let parameterDialog = screen.getByRole("dialog", { name: "Configure Controller Parameters" });
    expect(within(parameterDialog).getByLabelText("FW_RR_P")).toBeInTheDocument();
    expect(within(parameterDialog).getByLabelText("FW_RR_I")).toBeInTheDocument();
    expect(within(parameterDialog).queryByLabelText("FW_RR_D")).not.toBeInTheDocument();
    fireEvent.change(within(parameterDialog).getByLabelText("FW_RR_I"), { target: { value: "0.2" } });
    fireEvent.click(within(parameterDialog).getByRole("button", { name: "Apply" }));
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Configure Controller Parameters" })).not.toBeInTheDocument());

    fireEvent.change(screen.getByLabelText("Scenario"), { target: { value: "bundled:second.yaml" } });
    fireEvent.click(screen.getByRole("button", { name: "Configure" }));
    parameterDialog = screen.getByRole("dialog", { name: "Configure Controller Parameters" });
    expect(within(parameterDialog).queryByLabelText("FW_RR_P")).not.toBeInTheDocument();
    expect(within(parameterDialog).getByLabelText("FW_RR_I")).toHaveValue("0.2");
    expect(within(parameterDialog).getByLabelText("FW_RR_D")).toHaveValue("0");
    fireEvent.click(within(parameterDialog).getByRole("button", { name: "Apply" }));
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Configure Controller Parameters" })).not.toBeInTheDocument());
    fireEvent.click(screen.getByRole("button", { name: "Run simulation" }));
    await waitFor(() => expect(fetchMock).toHaveBeenCalledWith(
      "/api/runs",
      expect.objectContaining({
        method: "POST",
        body: JSON.stringify({
          scenario: "second.yaml",
          scenario_source: "bundled",
          branch: "impl",
          controller_parameters: { FW_RR_I: 0.2, FW_RR_D: 0 },
        }),
      }),
    ));
  });

  it("preserves all six supported Roll Hold parameters from Scenario whitelist to dialog", async () => {
    const ids = ["FW_R_TC", "FW_RR_P", "FW_RR_I", "FW_RR_D", "FW_RR_FF", "FW_RR_IMAX"];
    const defaults: Record<string, number> = {
      FW_R_TC: 0.4, FW_RR_P: 0.05, FW_RR_I: 0.1, FW_RR_D: 0, FW_RR_FF: 0.5, FW_RR_IMAX: 0.2,
    };
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      const body = path === "/api/scenarios" ? [{
        id: "roll_hold.yaml", name: "Roll hold", source: "bundled", valid: true,
        controller_parameters: ids, scenario_sha256: "a".repeat(64),
      }] : path === "/api/runtime/repository" ? {
        id: 1, default_branch: "impl", status: "ready",
      } : path === "/api/runtime/branches" ? [{
        name: "impl", commit_sha: "abc123456789", current: true, remote: true,
      }] : path === "/api/scenarios/sync/status" ? {
        configured: false, reachable: null, last_sync_at: null, last_success_at: null, last_error: null,
      } : path.startsWith("/api/runtime/variants") ? {
        branch: "impl", commit_sha: "abc123456789", mode: "compare", variants: ["baseline", "primary"],
      } : path.startsWith("/api/runtime/parameters") ? {
        branch: "impl", commit_sha: "abc123456789", parameters: ids.map((id) => ({
          id,
          display_name: id,
          module: "flight.roll",
          default_value: defaults[id],
          minimum: 0,
          maximum: 10,
          variants: ["baseline"],
        })),
      } : [];
      return Promise.resolve({ ok: true, status: 200, json: async () => body } as Response);
    }));
    render(<MemoryRouter><NewRunForm onClose={() => undefined} /></MemoryRouter>);

    await waitFor(() => expect(screen.getByRole("button", { name: "Configure" })).toBeEnabled());
    expect(screen.getByText("6 parameters")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Configure" }));
    const dialog = screen.getByRole("dialog", { name: "Configure Controller Parameters" });
    expect(within(dialog).getByRole("button", { name: "Roll 6" })).toBeInTheDocument();
    ids.forEach((id) => expect(within(dialog).getByLabelText(id)).toBeInTheDocument());
  });

  it("blocks the Run when the selected branch lacks a declared parameter", async () => {
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      const branch = path.includes("branch=main") ? "main" : "impl";
      const body = path === "/api/scenarios" ? [{
        id: "roll_hold.yaml", name: "Roll hold", source: "bundled", valid: true,
        controller_parameters: ["FW_RR_P", "FW_RR_FF"], scenario_sha256: "a".repeat(64),
      }] : path === "/api/runtime/repository" ? {
        id: 1, default_branch: "impl", status: "ready",
      } : path === "/api/runtime/branches" ? [
        { name: "impl", commit_sha: "abc123456789", current: true, remote: true },
        { name: "main", commit_sha: "def567890123", current: false, remote: true },
      ] : path === "/api/scenarios/sync/status" ? {
        configured: false, reachable: null, last_sync_at: null, last_success_at: null, last_error: null,
      } : path.startsWith("/api/runtime/variants") ? {
        branch, commit_sha: branch === "main" ? "def567890123" : "abc123456789",
        mode: "compare", variants: ["baseline", "primary"],
      } : path.startsWith("/api/runtime/parameters") ? {
        branch,
        commit_sha: branch === "main" ? "def567890123" : "abc123456789",
        parameters: [
          { id: "FW_RR_P", display_name: "Roll Rate P", default_value: 0.05, minimum: 0, maximum: 10, variants: ["baseline"] },
          ...(branch === "impl" ? [{ id: "FW_RR_FF", display_name: "Roll Rate FF", default_value: 0.5, minimum: 0, maximum: 10, variants: ["baseline"] }] : []),
        ],
      } : [];
      return Promise.resolve({ ok: true, status: 200, json: async () => body } as Response);
    }));
    render(<MemoryRouter><NewRunForm onClose={() => undefined} /></MemoryRouter>);

    await waitFor(() => expect(screen.getByRole("button", { name: "Run simulation" })).toBeEnabled());
    fireEvent.change(screen.getByLabelText("JSB0 Branch"), { target: { value: "main" } });
    expect(await screen.findByText(
      "Unsupported controller parameter for selected JSB0 revision: FW_RR_FF",
    )).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Run simulation" })).toBeDisabled();
  });

  it("shows compact branch resolving and unavailable states", async () => {
    let rejectVariantPreview: ((reason: Error) => void) | undefined;
    const unresolvedVariantPreview = new Promise<Response>((_, reject) => { rejectVariantPreview = reject; });
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path.startsWith("/api/runtime/variants")) return unresolvedVariantPreview;
      const body = path === "/api/scenarios" ? [{
        id: "roll_hold.yaml", name: "Roll hold", source: "bundled", valid: true,
        controller_parameters: ["FW_RR_P"],
        scenario_sha256: "a".repeat(64), validated_runtime_commit: null, last_validated_at: null,
      }] : path === "/api/runtime/repository" ? { id: 1, default_branch: "impl", status: "ready" }
        : path === "/api/runtime/branches" ? [{ name: "impl", commit_sha: "abc123456789", current: true, remote: true }]
          : path === "/api/scenarios/sync/status" ? { configured: false, reachable: null, last_sync_at: null, last_success_at: null, last_error: null }
            : path.startsWith("/api/runtime/parameters") ? {
              branch: "impl",
              commit_sha: "abc123456789",
              parameters: [{
                id: "FW_RR_P", display_name: "Roll Rate P", default_value: 0.05,
                minimum: 0, maximum: 10, variants: ["baseline"],
              }],
            }
              : [];
      return Promise.resolve({ ok: true, status: 200, json: async () => body } as Response);
    }));

    render(<MemoryRouter><NewRunForm onClose={() => undefined} /></MemoryRouter>);

    const branch = await screen.findByLabelText("JSB0 Branch");
    await waitFor(() => expect(branch).toHaveDisplayValue("impl · resolving…"));
    rejectVariantPreview?.(new Error("resolve failed"));
    await waitFor(() => expect(branch).toHaveDisplayValue("impl · unavailable"));
    expect(screen.queryByText(/Unsupported controller parameter/)).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Configure" })).toBeDisabled();
  });

  it("keeps execution creation separate from Run comparison analysis", async () => {
    const fetchMock = vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      const body = path === "/api/scenarios" ? [{
        id: "roll_hold.yaml", name: "Roll hold", source: "bundled", valid: true,
        scenario_sha256: "a".repeat(64), validated_runtime_commit: null, last_validated_at: null,
      }] : path === "/api/runtime/repository" ? {
        id: 1, default_branch: "impl", status: "ready",
      } : path === "/api/runtime/branches" ? [{
        name: "impl", commit_sha: "abc123456789", current: true, remote: true,
      }] : path === "/api/scenarios/sync/status" ? {
        configured: false, reachable: null, last_sync_at: null, last_success_at: null, last_error: null,
      } : path.startsWith("/api/runtime/variants") ? {
        branch: "impl", commit_sha: "abc123456789", mode: "compare", variants: ["baseline", "primary"],
      } : [];
      return Promise.resolve({ ok: true, status: 200, json: async () => body } as Response);
    });
    vi.stubGlobal("fetch", fetchMock);
    render(<MemoryRouter><NewRunForm onClose={() => undefined} /></MemoryRouter>);

    expect(await screen.findByText("This revision runs baseline + primary together in one Run.")).toBeInTheDocument();
    expect(screen.queryByLabelText("Execution Variant")).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "Compare" })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "Run comparison" })).not.toBeInTheDocument();
    expect(screen.queryByRole("region", { name: "Controller Parameters" })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "Configure" })).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Run simulation" })).toBeEnabled();
  });

  it("shows a system configuration error without exposing a repository selector", async () => {
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/api/scenarios") {
        return Promise.resolve({ ok: true, status: 200, json: async () => [{
          id: "roll_hold.yaml", name: "Roll hold", source: "bundled",
          autopilot: "baseline", valid: true, scenario_sha256: "a".repeat(64),
          validated_runtime_commit: null, last_validated_at: null,
        }] } as Response);
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
