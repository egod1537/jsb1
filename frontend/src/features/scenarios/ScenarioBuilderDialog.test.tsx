import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { ScenarioBuilderDialog } from "./ScenarioBuilderDialog";

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

function response(body: unknown, status = 200): Response {
  return {
    ok: status >= 200 && status < 300,
    status,
    json: async () => body,
  } as Response;
}

const validResult = {
  valid: true,
  scenario: { name: "New Roll Hold Scenario", autopilot: null },
  runtime: { branch: "main", commit: "a".repeat(40) },
  schema_version: 1,
  errors: [],
};

const controllerParameters = [
  ["FW_R_TC", "Roll Time Constant"],
  ["FW_RR_P", "Roll Rate P"],
  ["FW_RR_I", "Roll Rate I"],
  ["FW_RR_D", "Roll Rate D"],
  ["FW_RR_FF", "Roll Rate Feed Forward"],
  ["FW_RR_IMAX", "Roll Rate I Limit"],
].map(([id, display_name]) => ({
  id,
  display_name,
  description: `${display_name} description`,
  default_value: id === "FW_RR_P" ? 0.05 : 0,
  minimum: 0,
  maximum: 10,
  increment: 0.005,
  variants: ["baseline", "primary"],
}));

function parameterCatalog() {
  return {
    branch: "main",
    commit_sha: "a".repeat(40),
    source: "jsb1_px4_roll_hold_adapter",
    transport: "--parameters",
    parameters: controllerParameters,
  };
}

describe("ScenarioBuilderDialog", () => {
  it("builds the Roll Hold Test preset separately from controller parameters", async () => {
    vi.stubGlobal("fetch", vi.fn(() => Promise.resolve(response(parameterCatalog()))));
    render(<ScenarioBuilderDialog isOpen onClose={() => undefined} onSaved={() => undefined} />);
    const dialog = screen.getByRole("dialog", { name: "New Scenario" });
    const sectionNav = within(dialog).getByRole("navigation", { name: "Scenario sections" });
    expect(within(sectionNav).getAllByRole("button").map((button) => button.textContent)).toEqual([
      "General",
      "Initial Condition",
      "Environment",
      "Trim",
      "Simulation",
      "Events / Command",
      "Controller Parameters",
      "Acceptance",
    ]);
    expect(within(sectionNav).getByRole("button", { name: "General" })).toHaveAttribute("aria-current", "step");
    expect(screen.getByLabelText("Scenario preset")).toHaveValue("roll-hold-test");
    expect(screen.queryByLabelText("Latitude [deg]")).not.toBeInTheDocument();

    fireEvent.click(within(sectionNav).getByRole("button", { name: "Initial Condition" }));
    expect(screen.getByLabelText("Latitude [deg]")).toHaveValue("0");
    expect(screen.getByLabelText("p [rad/s]")).toHaveValue("0");
    expect(screen.queryByLabelText("Name")).not.toBeInTheDocument();

    fireEvent.click(within(sectionNav).getByRole("button", { name: "Simulation" }));
    expect(screen.getByLabelText("Time step [s]")).toHaveValue("0.01");
    fireEvent.click(within(sectionNav).getByRole("button", { name: "Controller Parameters" }));
    expect(await within(dialog).findByRole("checkbox", { name: /FW_RR_P/ })).toBeChecked();
    expect(within(dialog).getAllByRole("checkbox")).toHaveLength(6);
    const main = dialog.querySelector<HTMLElement>(".scenario-builder-main")!;
    expect(window.getComputedStyle(main).overflowY).toBe("auto");
    expect(window.getComputedStyle(dialog).display).toBe("flex");

    fireEvent.click(screen.getByRole("tab", { name: "Raw YAML" }));
    const yaml = screen.getByLabelText("Scenario YAML preview");
    expect(yaml).toHaveTextContent("events:");
    expect(yaml).toHaveTextContent("latitude_deg: 0");
    expect(yaml).toHaveTextContent("dt_sec: 0.01");
    expect(yaml).toHaveTextContent("settling_band_deg: 0.1");
    expect(yaml).not.toHaveTextContent("autopilot");
    expect(yaml).toHaveTextContent("controller_parameters:");
    expect(yaml).toHaveTextContent("- FW_RR_P");
    expect(yaml).not.toHaveTextContent("default_value");
  });

  it("requires validation again after edits and saves through the managed API", async () => {
    const onSaved = vi.fn();
    const fetchMock = vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
      const path = String(input);
      if (path.startsWith("/api/runtime/parameters")) {
        return Promise.resolve(response(parameterCatalog()));
      }
      if (path === "/api/scenarios/validate") {
        const body = JSON.parse(String(init?.body)) as { yaml: string };
        expect(body.yaml).toContain("scenario_type: roll_hold");
        return Promise.resolve(response(validResult));
      }
      if (path === "/api/scenarios" && init?.method === "POST") {
        const body = JSON.parse(String(init.body)) as { path: string; yaml: string };
        expect(body.path).toBe("revalidated_scenario.yaml");
        expect(body.yaml).toContain('name: "Revalidated Scenario"');
        return Promise.resolve(response({
          id: body.path,
          source: "managed",
          path: body.path,
          scenario_sha256: "b".repeat(64),
          validation: validResult,
        }, 201));
      }
      throw new Error(`unexpected request: ${path}`);
    });
    vi.stubGlobal("fetch", fetchMock);
    render(<ScenarioBuilderDialog isOpen onClose={() => undefined} onSaved={onSaved} />);

    const dialog = screen.getByRole("dialog", { name: "New Scenario" });
    const save = within(dialog).getByRole("button", { name: "Save Scenario" });
    expect(save).toBeDisabled();
    fireEvent.click(within(dialog).getByRole("button", { name: "Validate" }));
    await waitFor(() => expect(save).toBeEnabled());
    expect(within(dialog).getByText(/Valid against JSB0 main/)).toBeInTheDocument();
    expect(within(dialog).getAllByLabelText("Valid")).toHaveLength(8);

    fireEvent.change(within(dialog).getByLabelText("Name"), { target: { value: "Revalidated Scenario" } });
    expect(save).toBeDisabled();
    expect(within(dialog).getByText(/Needs validation/)).toBeInTheDocument();
    fireEvent.click(within(dialog).getByRole("button", { name: "Validate" }));
    await waitFor(() => expect(save).toBeEnabled());
    fireEvent.click(save);
    await waitFor(() => expect(onSaved).toHaveBeenCalledTimes(1));
  });

  it("keeps save disabled for schema errors and reports duplicate save conflicts", async () => {
    let saveAttempt = false;
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      if (path.startsWith("/api/runtime/parameters")) {
        return Promise.resolve(response(parameterCatalog()));
      }
      if (path === "/api/scenarios/validate") {
        return Promise.resolve(response({
          ...validResult,
          valid: saveAttempt,
          errors: saveAttempt ? [] : [{ path: "events", code: "required", message: "events is required" }],
        }));
      }
      if (path === "/api/scenarios") {
        return Promise.resolve(response({ detail: "scenario already exists: duplicate.yaml" }, 409));
      }
      throw new Error(`unexpected request: ${path}`);
    }));
    render(<ScenarioBuilderDialog isOpen onClose={() => undefined} onSaved={() => undefined} />);
    const dialog = screen.getByRole("dialog", { name: "New Scenario" });
    fireEvent.click(within(dialog).getByRole("button", { name: "Validate" }));
    await waitFor(() => expect(within(dialog).getByText((_, element) => (
      element?.tagName === "LI" && element.textContent?.includes("events is required") === true
    ))).toBeInTheDocument());
    expect(within(dialog).getByRole("button", { name: "Save Scenario" })).toBeDisabled();
    const sectionNav = within(dialog).getByRole("navigation", { name: "Scenario sections" });
    const eventsSection = within(sectionNav).getByRole("button", { name: /Events \/ Command/ });
    expect(eventsSection).toHaveAttribute("aria-current", "step");
    expect(within(eventsSection).getByLabelText("1 validation error")).toBeInTheDocument();

    saveAttempt = true;
    fireEvent.click(within(sectionNav).getByRole("button", { name: "General" }));
    fireEvent.change(within(dialog).getByLabelText("File path"), { target: { value: "duplicate.yaml" } });
    fireEvent.click(within(dialog).getByRole("button", { name: "Validate" }));
    await waitFor(() => expect(within(dialog).getByRole("button", { name: "Save Scenario" })).toBeEnabled());
    fireEvent.click(within(dialog).getByRole("button", { name: "Save Scenario" }));
    expect(await within(dialog).findByText("scenario already exists: duplicate.yaml")).toBeInTheDocument();
  });

  it("stores the tunable parameter whitelist and delegates Save & Run without values", async () => {
    const onRunRequested = vi.fn();
    vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
      const path = String(input);
      if (path.startsWith("/api/runtime/parameters")) return Promise.resolve(response(parameterCatalog()));
      if (path === "/api/scenarios/validate") {
        const body = JSON.parse(String(init?.body)) as { yaml: string };
        expect(body.yaml).toContain("controller_parameters:\n  - FW_R_TC\n  - FW_RR_P");
        expect(body.yaml).not.toContain("default_value");
        return Promise.resolve(response(validResult));
      }
      if (path === "/api/scenarios") return Promise.resolve(response({
        id: "roll_hold_test.yaml", source: "managed", path: "roll_hold_test.yaml",
        scenario_sha256: "b".repeat(64), validation: validResult,
      }, 201));
      throw new Error(`unexpected request: ${path}`);
    }));
    render(<ScenarioBuilderDialog
      isOpen
      onClose={() => undefined}
      onRunRequested={onRunRequested}
      onSaved={() => undefined}
    />);
    const dialog = screen.getByRole("dialog", { name: "New Scenario" });
    fireEvent.click(within(dialog).getByRole("button", { name: "Controller Parameters" }));
    expect(await within(dialog).findByRole("checkbox", { name: /FW_RR_P/ })).toBeChecked();
    fireEvent.click(within(dialog).getByRole("button", { name: "Validate" }));
    const saveAndRun = within(dialog).getByRole("button", { name: "Save & Run" });
    await waitFor(() => expect(saveAndRun).toBeEnabled());
    fireEvent.click(saveAndRun);
    await waitFor(() => expect(onRunRequested).toHaveBeenCalledWith(
      expect.objectContaining({ id: "roll_hold_test.yaml" }),
    ));
  });
});
