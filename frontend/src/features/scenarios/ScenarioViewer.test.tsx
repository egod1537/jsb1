import { act, cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { ScenarioInspectionDetail } from "../../types/api";
import { ScenarioLibraryPage } from "../../pages/ScenarioLibraryPage";
import { ScenarioViewer } from "./ScenarioViewer";

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

const rollHold: ScenarioInspectionDetail = {
  id: "bundled:samples/roll.yaml",
  source: "bundled",
  path: "samples/roll.yaml",
  name: "Roll Hold 5deg 30s",
  scenario_type: "roll_hold",
  schema_version: 1,
  sha256: "a".repeat(64),
  updated_at: "2026-08-30T00:00:00Z",
  validation: { valid: true, runtime_branch: "main", runtime_commit: "b".repeat(40), errors: [] },
  provenance: { authority: "repository file", expected_sha256: "a".repeat(64), actual_sha256: "a".repeat(64), integrity: "verified" },
  definition: {
    schema_version: 1,
    scenario_type: "roll_hold",
    name: "Roll Hold 5deg 30s",
    aircraft: "c172x",
    initial_condition: { altitude_ft: 3000, airspeed_kts: 100, roll_deg: 0, pitch_deg: 0, heading_deg: 0 },
    environment: { wind_enabled: false },
    trim: { enabled: false, mode: "Full" },
    simulation: { duration_sec: 30 },
    command: { start_sec: 5, roll_deg: 5 },
    acceptance: { settling_band_deg: 0.1, settling_time_limit_sec: 20, overshoot_limit_deg: 5, max_oscillation_cycles: 10 },
  },
  raw_yaml: "# original\nname: Roll Hold 5deg 30s\n",
};

function response(body: unknown, status = 200): Response {
  return { ok: status >= 200 && status < 300, status, json: async () => body } as Response;
}

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (reason: unknown) => void;
  const promise = new Promise<T>((resolvePromise, rejectPromise) => {
    resolve = resolvePromise;
    reject = rejectPromise;
  });
  return { promise, resolve, reject };
}

describe("ScenarioViewer", () => {
  it("renders semantic roll-hold sections and preserves raw YAML", () => {
    render(<ScenarioViewer scenario={rollHold} />);
    expect(screen.getByText("repository file")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("tab", { name: "Definition" }));
    expect(screen.getByText("Initial Conditions")).toBeInTheDocument();
    expect(screen.getByText("3000 ft")).toBeInTheDocument();
    expect(screen.getByText("Command / Events")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("tab", { name: "Raw YAML" }));
    expect(screen.getByText(/# original/)).toHaveTextContent("# original");
  });

  it("falls back to a generic definition for a new scenario type", () => {
    render(<ScenarioViewer scenario={{
      ...rollHold,
      id: "sftp:new.yaml",
      source: "sftp",
      scenario_type: "formation_flight",
      definition: { scenario_type: "formation_flight", formation: { spacing_m: 25 } },
      provenance: { ...rollHold.provenance, authority: "validated SFTP cache" },
    }} />);
    fireEvent.click(screen.getByRole("tab", { name: "Definition" }));
    expect(screen.getByText("Formation")).toBeInTheDocument();
    expect(screen.getByText("25")).toBeInTheDocument();
  });

  it("presents structured validation errors without parsing messages", () => {
    render(<ScenarioViewer scenario={{
      ...rollHold,
      validation: {
        valid: false,
        runtime_branch: "main",
        runtime_commit: "b".repeat(40),
        errors: [{ path: "command.roll_deg", code: "type", message: "Expected number" }],
      },
    }} />);
    fireEvent.click(screen.getByRole("tab", { name: "Validation" }));
    expect(screen.getByText("command.roll_deg")).toBeInTheDocument();
    expect(screen.getByText("Expected number")).toBeInTheDocument();
    expect(screen.getByText("type")).toBeInTheDocument();
  });
});

describe("ScenarioLibraryPage", () => {
  it("creates a validated managed scenario, refreshes the catalog, and opens its detail", async () => {
    const managedDetail: ScenarioInspectionDetail = {
      ...rollHold,
      id: "managed:new_roll_hold_scenario.yaml",
      source: "managed",
      path: "new_roll_hold_scenario.yaml",
      name: "New Roll Hold Scenario",
      provenance: {
        authority: "managed scenario",
        expected_sha256: "c".repeat(64),
        actual_sha256: "c".repeat(64),
        integrity: "verified",
      },
    };
    let catalog = [rollHold].map(({ definition: _definition, raw_yaml: _raw, provenance: _provenance, ...item }) => item);
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
      const path = String(input);
      if (path === "/api/scenario-catalog") return Promise.resolve(response(catalog));
      if (path === "/api/scenarios/sync/status") return Promise.resolve(response({ configured: false }));
      if (path === "/api/scenarios/validate") return Promise.resolve(response({
        valid: true,
        scenario: { name: "New Roll Hold Scenario", autopilot: null },
        runtime: { branch: "main", commit: "d".repeat(40) },
        schema_version: 1,
        errors: [],
      }));
      if (path === "/api/scenarios" && init?.method === "POST") {
        catalog = [...catalog, (({ definition: _definition, raw_yaml: _raw, provenance: _provenance, ...item }) => item)(managedDetail)];
        return Promise.resolve(response({
          id: "new_roll_hold_scenario.yaml",
          source: "managed",
          path: "new_roll_hold_scenario.yaml",
          scenario_sha256: "c".repeat(64),
          validation: {
            valid: true,
            scenario: { name: "New Roll Hold Scenario", autopilot: null },
            runtime: { branch: "main", commit: "d".repeat(40) },
            schema_version: 1,
            errors: [],
          },
        }, 201));
      }
      if (path.startsWith("/api/scenario-catalog/detail?") && path.includes("source=managed")) {
        return Promise.resolve(response(managedDetail));
      }
      return Promise.resolve(response(rollHold));
    }));
    render(<MemoryRouter initialEntries={["/scenarios"]}>
      <Routes><Route path="/scenarios" element={<ScenarioLibraryPage />} /></Routes>
    </MemoryRouter>);
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Loading" })).not.toBeInTheDocument());

    fireEvent.click(screen.getByRole("button", { name: "New Scenario" }));
    const builder = await screen.findByRole("dialog", { name: "New Scenario" });
    fireEvent.click(within(builder).getByRole("button", { name: "Validate" }));
    await waitFor(() => expect(within(builder).getByRole("button", { name: "Save Scenario" })).toBeEnabled());
    fireEvent.click(within(builder).getByRole("button", { name: "Save Scenario" }));

    const table = await screen.findByRole("region", { name: "Scenario list" });
    await waitFor(() => expect(within(table).getAllByText("New Roll Hold Scenario")).toHaveLength(1));
    const detail = await screen.findByRole("dialog", { name: "New Roll Hold Scenario" });
    expect(within(detail).getByText("managed scenario")).toBeInTheDocument();
  });

  it("shows initial catalog loading in a centered modal instead of inline content", async () => {
    const catalogRequest = deferred<Response>();
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/api/scenario-catalog") return catalogRequest.promise;
      if (path === "/api/scenarios/sync/status") return Promise.resolve(response({ configured: false }));
      return Promise.resolve(response([]));
    }));
    render(<MemoryRouter initialEntries={["/scenarios"]}>
      <Routes><Route path="/scenarios" element={<ScenarioLibraryPage />} /></Routes>
    </MemoryRouter>);

    const dialog = await screen.findByRole("dialog", { name: "Loading" });
    expect(within(dialog).getByRole("status")).toHaveTextContent("Loading scenario catalog...");
    const tableArea = screen.getByRole("region", { name: "Scenario list" });
    const page = tableArea.closest<HTMLElement>(".scenario-library-page")!;
    const content = tableArea.closest<HTMLElement>(".scenario-library-content")!;
    expect(window.getComputedStyle(page).display).toBe("flex");
    expect(window.getComputedStyle(page).flexDirection).toBe("column");
    expect(window.getComputedStyle(content).flexGrow).toBe("1");
    expect(window.getComputedStyle(content).minHeight).toBe("0");
    expect(window.getComputedStyle(tableArea).flexGrow).toBe("1");
    expect(window.getComputedStyle(tableArea).minHeight).toBe("0");
    expect(window.getComputedStyle(tableArea).overflowY).toBe("auto");
    expect(window.getComputedStyle(tableArea.querySelector("table")!).minWidth).toBe("0");

    await act(async () => catalogRequest.resolve(response([])));
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Loading" })).not.toBeInTheDocument());
  });

  it("loads lightweight catalog, filters it, and fetches selected detail on demand", async () => {
    const invalid = {
      ...rollHold,
      id: "sftp:bad.yaml",
      source: "sftp" as const,
      path: "bad.yaml",
      name: "Broken remote",
      provenance: { ...rollHold.provenance, authority: "validated SFTP cache" },
      validation: { valid: false, runtime_branch: "main", runtime_commit: "b".repeat(40), errors: [{ path: "command.roll_deg", code: "type", message: "Expected number" }] },
    };
    const fetchMock = vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/api/scenario-catalog") return Promise.resolve(response([rollHold, invalid].map(({ definition: _definition, raw_yaml: _raw, provenance: _provenance, ...item }) => item)));
      if (path === "/api/scenarios/sync/status") return Promise.resolve(response({ configured: false, reachable: null, last_sync_at: null, last_success_at: null, last_error: null }));
      if (path.startsWith("/api/scenario-catalog/detail?")) return Promise.resolve(response(path.includes("bad.yaml") ? invalid : rollHold));
      return Promise.resolve(response([]));
    });
    vi.stubGlobal("fetch", fetchMock);
    render(<MemoryRouter initialEntries={["/scenarios?id=bundled%3Asamples%2Froll.yaml"]}>
      <Routes><Route path="/scenarios" element={<ScenarioLibraryPage />} /></Routes>
    </MemoryRouter>);

    const table = await screen.findByRole("region", { name: "Scenario list" });
    expect(within(table).getByText("Roll Hold 5deg 30s")).toBeInTheDocument();
    expect(within(table).getByText("Broken remote")).toBeInTheDocument();
    const detailDialog = await screen.findByRole("dialog", { name: "Roll Hold 5deg 30s" });
    expect(within(detailDialog).getByText("repository file")).toBeInTheDocument();
    expect(within(detailDialog).getAllByRole("tab").map((tab) => tab.textContent)).toEqual([
      "Overview", "Definition", "Raw YAML", "Validation",
    ]);
    expect(screen.queryByLabelText("Scenario inspector")).not.toBeInTheDocument();
    expect(table).toHaveClass("scenario-list-full-width");
    fireEvent.click(within(detailDialog).getByRole("button", { name: "Close" }));
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Roll Hold 5deg 30s" })).not.toBeInTheDocument());

    fireEvent.change(screen.getByLabelText("Search scenarios"), { target: { value: "broken" } });
    expect(within(table).queryByText("Roll Hold 5deg 30s")).not.toBeInTheDocument();
    expect(within(table).getByText("Broken remote")).toBeInTheDocument();
    fireEvent.change(screen.getByLabelText("Search scenarios"), { target: { value: "" } });
    fireEvent.click(screen.getByRole("button", { name: "Filters" }));
    const filters = await screen.findByLabelText("Scenario filters");
    fireEvent.click(within(within(filters).getByRole("radiogroup", { name: "Validation" })).getByRole("radio", { name: "Invalid" }));
    expect(within(table).queryByText("Roll Hold 5deg 30s")).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Filters, 1 active" })).toHaveTextContent("Filters (1)");
    fireEvent.click(within(filters).getByRole("button", { name: "Done" }));
    fireEvent.keyDown(within(table).getByText("Broken remote").closest("tr")!, { key: "Enter" });
    await waitFor(() => expect(fetchMock.mock.calls.some(([input]) => String(input).includes("bad.yaml"))).toBe(true));
    const invalidDialog = await screen.findByRole("dialog", { name: "Broken remote" });
    fireEvent.click(within(invalidDialog).getByRole("tab", { name: "Validation" }));
    expect(within(invalidDialog).getByText("Expected number")).toBeInTheDocument();
    fireEvent.click(within(invalidDialog).getByRole("button", { name: "Close" }));
    expect(screen.getByLabelText("Search scenarios")).toHaveValue("");
    fireEvent.click(screen.getByRole("button", { name: "Filters, 1 active" }));
    expect(within(screen.getByLabelText("Scenario filters")).getByRole("radio", { name: "Invalid" })).toBeChecked();
  });

  it("combines source, type, and validation filters, reports active filters, and clears them without resetting search or sort", async () => {
    const invalid = {
      ...rollHold,
      id: "sftp:bad.yaml", source: "sftp" as const, path: "bad.yaml", name: "Broken remote",
      validation: { valid: false, runtime_branch: "main", runtime_commit: "b".repeat(40), errors: [] },
    };
    const altitude = {
      ...rollHold,
      id: "sftp:altitude.yaml", source: "sftp" as const, path: "altitude.yaml", name: "Altitude sample",
      scenario_type: "altitude_hold", validation: { valid: true, runtime_branch: "main", runtime_commit: "b".repeat(40), errors: [] },
    };
    const catalog = [rollHold, invalid, altitude]
      .map(({ definition: _definition, raw_yaml: _raw, provenance: _provenance, ...item }) => item);
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/api/scenario-catalog") return Promise.resolve(response(catalog));
      if (path === "/api/scenarios/sync/status") return Promise.resolve(response({ configured: false }));
      return Promise.resolve(response([]));
    }));
    render(<MemoryRouter initialEntries={["/scenarios"]}>
      <Routes><Route path="/scenarios" element={<ScenarioLibraryPage />} /></Routes>
    </MemoryRouter>);

    const table = await screen.findByRole("region", { name: "Scenario list" });
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Loading" })).not.toBeInTheDocument());
    expect(screen.queryByLabelText("Source filter")).not.toBeInTheDocument();
    expect(screen.queryByLabelText("Type filter")).not.toBeInTheDocument();
    expect(screen.queryByLabelText("Validation filter")).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Filters" }));
    const filters = await screen.findByLabelText("Scenario filters");
    fireEvent.click(within(within(filters).getByRole("radiogroup", { name: "Source" })).getByRole("radio", { name: "SFTP" }));
    fireEvent.click(within(within(filters).getByRole("radiogroup", { name: "Type" })).getByRole("radio", { name: "roll_hold" }));
    fireEvent.click(within(within(filters).getByRole("radiogroup", { name: "Validation" })).getByRole("radio", { name: "Invalid" }));
    expect(screen.getByRole("button", { name: "Filters, 3 active" })).toHaveTextContent("Filters (3)");
    expect(within(table).getByText("Broken remote")).toBeInTheDocument();
    expect(within(table).queryByText("Roll Hold 5deg 30s")).not.toBeInTheDocument();
    expect(within(table).queryByText("Altitude sample")).not.toBeInTheDocument();

    fireEvent.change(screen.getByLabelText("Search scenarios"), { target: { value: "broken" } });
    fireEvent.click(within(table).getByRole("button", { name: "Name" }));
    expect(within(table).getByRole("columnheader", { name: "Name" })).toHaveAttribute("aria-sort", "ascending");
    fireEvent.click(within(filters).getByRole("button", { name: "Clear" }));
    expect(screen.getByRole("button", { name: "Filters" })).toHaveTextContent("Filters");
    expect(screen.getByLabelText("Search scenarios")).toHaveValue("broken");
    expect(within(table).getByRole("columnheader", { name: "Name" })).toHaveAttribute("aria-sort", "ascending");
    expect(within(table).getByText("Broken remote")).toBeInTheDocument();

    fireEvent.change(screen.getByLabelText("Search scenarios"), { target: { value: "" } });
    expect(within(table).getByText("Roll Hold 5deg 30s")).toBeInTheDocument();
    expect(within(table).getByText("Altitude sample")).toBeInTheDocument();
    fireEvent.click(within(filters).getByRole("button", { name: "Done" }));
    await waitFor(() => expect(screen.queryByLabelText("Scenario filters")).not.toBeInTheDocument());
  });

  it("sorts every catalog column, preserves sort through filtering, and still opens row details", async () => {
    const zulu = { ...rollHold, name: "Zulu", schema_version: 2, updated_at: "2026-01-01T00:00:00Z" };
    const alpha = {
      ...rollHold,
      id: "sftp:alpha.yaml", source: "sftp" as const, path: "alpha.yaml", name: "alpha",
      scenario_type: "altitude_hold", schema_version: 10, updated_at: "2026-01-03T00:00:00Z",
      validation: { valid: false, runtime_branch: "main", runtime_commit: "b".repeat(40), errors: [] },
    };
    const bravo = {
      ...rollHold,
      id: "bundled:bravo.yaml", path: "bravo.yaml", name: "Bravo",
      scenario_type: null, schema_version: null, updated_at: "2026-01-02T00:00:00Z",
      validation: { valid: null, runtime_branch: "main", runtime_commit: "b".repeat(40), errors: [] },
    };
    const details = [zulu, alpha, bravo];
    const catalog = details.map(({ definition: _definition, raw_yaml: _raw, provenance: _provenance, ...item }) => item);
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/api/scenario-catalog") return Promise.resolve(response(catalog));
      if (path === "/api/scenarios/sync/status") return Promise.resolve(response({ configured: false }));
      if (path.startsWith("/api/scenario-catalog/detail?")) {
        const selected = details.find((item) => path.includes(encodeURIComponent(item.path))) ?? zulu;
        return Promise.resolve(response(selected));
      }
      return Promise.resolve(response([]));
    }));
    render(<MemoryRouter initialEntries={["/scenarios"]}>
      <Routes><Route path="/scenarios" element={<ScenarioLibraryPage />} /></Routes>
    </MemoryRouter>);

    const table = await screen.findByRole("region", { name: "Scenario list" });
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Loading" })).not.toBeInTheDocument());
    const rowNames = () => within(table).getAllByRole("row").slice(1)
      .map((row) => row.querySelector("strong")?.textContent);
    const dataRows = () => within(table).getAllByRole("row").slice(1);
    const sort = (label: string) => fireEvent.click(within(table).getByRole("button", { name: label }));

    expect(within(table).getByRole("columnheader", { name: "Updated" })).toHaveAttribute("aria-sort", "descending");
    expect(rowNames()).toEqual(["alpha", "Bravo", "Zulu"]);
    expect(dataRows()[0]).toHaveClass("scenario-row-odd");
    expect(dataRows()[1]).toHaveClass("scenario-row-even");

    sort("Name");
    expect(rowNames()).toEqual(["alpha", "Bravo", "Zulu"]);
    expect(within(table).getByRole("columnheader", { name: "Name" })).toHaveAttribute("aria-sort", "ascending");
    sort("Name");
    expect(rowNames()).toEqual(["Zulu", "Bravo", "alpha"]);

    sort("Type");
    expect(rowNames()).toEqual(["alpha", "Zulu", "Bravo"]);
    sort("Type");
    expect(rowNames()).toEqual(["Zulu", "alpha", "Bravo"]);

    sort("Source");
    expect(rowNames()).toEqual(["Zulu", "Bravo", "alpha"]);
    sort("Source");
    expect(rowNames()).toEqual(["alpha", "Zulu", "Bravo"]);

    sort("Schema");
    expect(rowNames()).toEqual(["Zulu", "alpha", "Bravo"]);
    sort("Schema");
    expect(rowNames()).toEqual(["alpha", "Zulu", "Bravo"]);

    sort("Validation");
    expect(rowNames()).toEqual(["alpha", "Bravo", "Zulu"]);
    sort("Validation");
    expect(rowNames()).toEqual(["Zulu", "Bravo", "alpha"]);

    sort("Updated");
    expect(rowNames()).toEqual(["Zulu", "Bravo", "alpha"]);
    sort("Updated");
    expect(rowNames()).toEqual(["alpha", "Bravo", "Zulu"]);

    fireEvent.change(screen.getByLabelText("Search scenarios"), { target: { value: "bravo" } });
    expect(rowNames()).toEqual(["Bravo"]);
    expect(dataRows()[0]).toHaveClass("scenario-row-odd");
    expect(within(table).getByRole("columnheader", { name: "Updated" })).toHaveAttribute("aria-sort", "descending");
    fireEvent.keyDown(within(table).getByText("Bravo").closest("tr")!, { key: "Enter" });
    expect(await screen.findByRole("dialog", { name: "Bravo" })).toBeInTheDocument();
    expect(within(table).getByText("Bravo").closest("tr")).toHaveClass("is-selected");
  });

  it("keeps the full-width list visible while loading, then opens the selected detail dialog", async () => {
    const nextDetailRequest = deferred<Response>();
    const remote = {
      ...rollHold,
      id: "sftp:next.yaml",
      source: "sftp" as const,
      path: "next.yaml",
      name: "Next remote",
      provenance: { ...rollHold.provenance, authority: "validated SFTP cache" },
    };
    const catalog = [rollHold, remote].map(({ definition: _definition, raw_yaml: _raw, provenance: _provenance, ...item }) => item);
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/api/scenario-catalog") return Promise.resolve(response(catalog));
      if (path === "/api/scenarios/sync/status") return Promise.resolve(response({ configured: false }));
      if (path.includes("next.yaml")) return nextDetailRequest.promise;
      if (path.startsWith("/api/scenario-catalog/detail?")) return Promise.resolve(response(rollHold));
      return Promise.resolve(response([]));
    }));
    render(<MemoryRouter initialEntries={["/scenarios?id=bundled%3Asamples%2Froll.yaml"]}>
      <Routes><Route path="/scenarios" element={<ScenarioLibraryPage />} /></Routes>
    </MemoryRouter>);

    const initialDialog = await screen.findByRole("dialog", { name: "Roll Hold 5deg 30s" });
    expect(within(initialDialog).getByText("repository file")).toBeInTheDocument();
    fireEvent.click(within(initialDialog).getByRole("button", { name: "Close" }));
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Loading" })).not.toBeInTheDocument());
    fireEvent.click(screen.getByText("Next remote").closest("tr")!);
    const dialog = await screen.findByRole("dialog", { name: "Loading" });
    await waitFor(() => expect(within(dialog).getByRole("status")).toHaveTextContent("Loading scenario..."));
    expect(screen.getByRole("region", { name: "Scenario list" })).toBeInTheDocument();
    expect(screen.queryByRole("dialog", { name: "Next remote" })).not.toBeInTheDocument();
    expect(screen.queryByText("Loading scenario definition")).not.toBeInTheDocument();

    await act(async () => nextDetailRequest.resolve(response(remote)));
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Loading" })).not.toBeInTheDocument());
    const nextDialog = await screen.findByRole("dialog", { name: "Next remote" });
    expect(within(nextDialog).getByText("validated SFTP cache")).toBeInTheDocument();
  });

  it("closes detail loading and shows a list-level error when detail fetch fails", async () => {
    const failedRequest = deferred<Response>();
    const broken = {
      ...rollHold,
      id: "sftp:broken.yaml",
      source: "sftp" as const,
      path: "broken.yaml",
      name: "Broken detail",
    };
    const catalog = [rollHold, broken].map(({ definition: _definition, raw_yaml: _raw, provenance: _provenance, ...item }) => item);
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/api/scenario-catalog") return Promise.resolve(response(catalog));
      if (path === "/api/scenarios/sync/status") return Promise.resolve(response({ configured: false }));
      if (path.includes("broken.yaml")) return failedRequest.promise;
      if (path.startsWith("/api/scenario-catalog/detail?")) return Promise.resolve(response(rollHold));
      return Promise.resolve(response([]));
    }));
    render(<MemoryRouter initialEntries={["/scenarios?id=bundled%3Asamples%2Froll.yaml"]}>
      <Routes><Route path="/scenarios" element={<ScenarioLibraryPage />} /></Routes>
    </MemoryRouter>);

    const initialDialog = await screen.findByRole("dialog", { name: "Roll Hold 5deg 30s" });
    expect(within(initialDialog).getByText("repository file")).toBeInTheDocument();
    fireEvent.click(within(initialDialog).getByRole("button", { name: "Close" }));
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Loading" })).not.toBeInTheDocument());
    fireEvent.click(screen.getByText("Broken detail").closest("tr")!);
    const dialog = await screen.findByRole("dialog", { name: "Loading" });
    await waitFor(() => expect(within(dialog).getByRole("status")).toHaveTextContent("Loading scenario..."));
    await act(async () => failedRequest.reject(new Error("Scenario detail unavailable")));
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Loading" })).not.toBeInTheDocument());
    expect(await screen.findByRole("alert")).toHaveTextContent("Scenario detail unavailable");
  });
});
