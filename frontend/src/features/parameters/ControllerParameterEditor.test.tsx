import { cleanup, fireEvent, render, screen, within } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { ControllerParameterConfigureDialog } from "./ControllerParameterConfigureDialog";
import {
  ControllerParameterEditor,
  controllerParameterCategories,
  controllerParameterErrors,
  controllerParameterOverrides,
  defaultControllerParameterValues,
  parseControllerParameterDrafts,
  selectSupportedControllerParameterDefinitions,
} from "./ControllerParameterEditor";
import { runtimeParameterViewModel } from "./parameterViewModel";

const definitions = [{
  id: "FW_RR_P",
  display_name: "Roll Rate P",
  symbol: "K_P",
  unit: "%/rad/s",
  default_value: 0.05,
  minimum: 0,
  maximum: 10,
  increment: 0.005,
  description: "Roll-rate proportional gain.",
  module: "flight.roll",
  variants: ["baseline"],
}, {
  id: "CUSTOM_P_GAIN",
  display_name: "Pitch Gain",
  category: "Pitch",
  unit: null,
  default_value: 0.1,
  minimum: 0,
  maximum: 1,
  step: 0.01,
  description: "Pitch gain supplied by Runtime metadata.",
  variants: ["baseline"],
}];

const rollHoldDefinitions = [
  { id: "FW_R_TC", display_name: "Roll Time Constant", module: "flight.roll", unit: "s", default_value: 0.4, minimum: 0.2, maximum: 1, variants: ["baseline"] },
  definitions[0],
  { id: "FW_RR_I", display_name: "Roll Rate I", module: "flight.roll", default_value: 0.1, minimum: 0, maximum: 10, variants: ["baseline"] },
  { id: "FW_RR_D", display_name: "Roll Rate D", module: "flight.roll", default_value: 0, minimum: 0, maximum: 10, variants: ["baseline"] },
  { id: "FW_RR_FF", display_name: "Roll Rate Feed Forward", module: "flight.roll", default_value: 0.5, minimum: 0, maximum: 10, variants: ["baseline"] },
  { id: "FW_RR_IMAX", display_name: "Roll Integrator Limit", module: "flight.roll", default_value: 0.2, minimum: 0, maximum: 1, variants: ["baseline"] },
];

const courseHoldDefinitions = [
  { id: "NPFG_PERIOD", display_name: "Guidance Period", group: "PX4 Course / Lateral Guidance", unit: "s", default_value: 10, minimum: 1, maximum: 100, increment: 0.1, variants: ["baseline"], scenario_types: ["course_hold"] },
  { id: "NPFG_DAMPING", display_name: "Guidance Damping", group: "PX4 Course / Lateral Guidance", unit: "dimensionless", default_value: 0.7, minimum: 0.1, maximum: 1, increment: 0.01, variants: ["baseline"], scenario_types: ["course_hold"] },
  { id: "FW_R_LIM", display_name: "Roll Limit", group: "PX4 Course / Lateral Guidance", unit: "deg", default_value: 20, minimum: 0, maximum: 75, increment: 0.5, variants: ["baseline"], scenario_types: ["course_hold"] },
  { id: "FW_PN_R_SLEW_MAX", display_name: "Roll Setpoint Slew", group: "PX4 Course / Lateral Guidance", unit: "deg/s", default_value: 30, minimum: 0, maximum: 180, increment: 1, variants: ["baseline"], scenario_types: ["course_hold"] },
  { ...rollHoldDefinitions[0], scenario_types: ["roll_hold"] },
];

const pitchHoldDefinitions = [
  ["FW_P_TC", "s", 0.2, 1, 0.2, 0.05],
  ["FW_P_RMAX_POS", "deg/s", 0, 180, 14, 0.5],
  ["FW_P_RMAX_NEG", "deg/s", 0, 180, 10, 0.5],
  ["FW_PR_P", "%/rad/s", 0, 10, 4.5, 0.005],
  ["FW_PR_I", "%/rad", 0, 10, 4.5, 0.005],
  ["FW_PR_D", "%/rad/s", 0, 10, 0, 0.005],
  ["FW_PR_FF", "%/rad/s", -10, 10, 1.2, 0.05],
  ["FW_PR_IMAX", "normalized", 0, 1, 0.4, 0.05],
].map(([id, unit, minimum, maximum, defaultValue, increment]) => ({
  id: String(id),
  display_name: String(id),
  group: "PX4 Pitch",
  module: "px4.pitch",
  unit: String(unit),
  minimum: Number(minimum),
  maximum: Number(maximum),
  default_value: Number(defaultValue),
  increment: Number(increment),
  variants: ["baseline"],
  scenario_types: ["pitch_hold"],
}));

const tecsIds = [
  "FW_P_LIM_MIN", "FW_P_LIM_MAX", "FW_THR_MIN", "FW_THR_MAX",
  "FW_AIRSPD_MIN", "FW_AIRSPD_MAX", "FW_T_CLMB_MAX", "FW_T_SINK_MAX",
  "FW_T_HRATE_P", "FW_T_SPDWEIGHT_P", "FW_T_THR_DAMP", "FW_T_I_GAIN_THR",
  "FW_T_PTCH_DAMP", "FW_T_I_GAIN_PIT", "FW_T_SEB_R_FF", "FW_T_STE_R_TC",
  "FW_T_PITCH_RATE", "FW_T_THR_SLEW", "FW_THR_TRIM",
] as const;

const tecsDefinitions = tecsIds.map((id) => {
  const group = ["FW_P_LIM_MIN", "FW_P_LIM_MAX", "FW_THR_MIN", "FW_THR_MAX", "FW_AIRSPD_MIN", "FW_AIRSPD_MAX"].includes(id)
    ? "PX4 TECS / Envelope"
    : ["FW_T_CLMB_MAX", "FW_T_SINK_MAX"].includes(id)
      ? "PX4 TECS / Performance"
      : ["FW_T_PITCH_RATE", "FW_T_THR_SLEW"].includes(id)
        ? "PX4 TECS / Slew"
        : id === "FW_THR_TRIM" ? "PX4 TECS / Trim" : "PX4 TECS / Energy Loop";
  const angle = id === "FW_P_LIM_MIN" || id === "FW_P_LIM_MAX" || id === "FW_T_PITCH_RATE";
  return {
    id,
    display_name: id,
    group,
    module: "px4.tecs",
    unit: angle ? "rad" : "ratio",
    display_unit: angle ? (id === "FW_T_PITCH_RATE" ? "deg/s" : "deg") : "ratio",
    display_scale: angle ? 180 / Math.PI : 1,
    minimum: 0,
    maximum: angle ? Math.PI / 4 : 10,
    default_value: angle ? Math.PI / 9 : 1,
    increment: angle ? Math.PI / 360 : 0.1,
    variants: ["baseline"],
    scenario_types: ["tecs"],
  };
});

afterEach(cleanup);

describe("ControllerParameterEditor", () => {
  it("shows Runtime metadata and preserves the numeric input string", () => {
    const onChange = vi.fn();
    render(<ControllerParameterEditor
      definitions={[definitions[0]]}
      draftValues={{ FW_RR_P: "0.05" }}
      onChange={onChange}
      variants={["baseline"]}
    />);
    expect(screen.getByText("Roll Rate P")).toBeInTheDocument();
    expect(screen.getByText("FW_RR_P")).toBeInTheDocument();
    expect(screen.getByText("%/rad/s")).toBeInTheDocument();
    fireEvent.change(screen.getByLabelText("FW_RR_P"), { target: { value: "0." } });
    expect(onChange).toHaveBeenCalledWith({ FW_RR_P: "0." });
  });

  it("resolves defaults, overrides, bounds, categories, and variant applicability", () => {
    expect(defaultControllerParameterValues(definitions, "baseline")).toEqual({ FW_RR_P: 0.05, CUSTOM_P_GAIN: 0.1 });
    expect(controllerParameterOverrides(definitions, { FW_RR_P: 0.08, CUSTOM_P_GAIN: 0.1 }, "baseline")).toEqual({ FW_RR_P: 0.08 });
    expect(controllerParameterErrors(definitions, { FW_RR_P: 11, CUSTOM_P_GAIN: 0.1 }, "baseline")).toEqual(["FW_RR_P must be at most 10"]);
    expect(defaultControllerParameterValues(definitions, "primary")).toEqual({});
    expect(controllerParameterCategories(definitions, "baseline")).toEqual([
      { id: "roll", label: "Roll" },
      { id: "pitch", label: "Pitch" },
    ]);
    expect(parseControllerParameterDrafts(definitions, {
      FW_RR_P: "0.",
      CUSTOM_P_GAIN: ".5",
    }, "baseline")).toEqual({ values: { FW_RR_P: 0, CUSTOM_P_GAIN: 0.5 }, errors: {} });
    expect(selectSupportedControllerParameterDefinitions(
      rollHoldDefinitions,
      ["FW_R_TC", "FW_RR_P", "FW_RR_I", "FW_RR_D", "FW_RR_FF", "FW_RR_IMAX"],
      ["baseline", "primary"],
    )).toHaveLength(6);
  });

  it("builds the Course editor from revision metadata and scenario applicability", () => {
    const selected = selectSupportedControllerParameterDefinitions(
      courseHoldDefinitions,
      ["NPFG_PERIOD", "NPFG_DAMPING", "FW_R_LIM", "FW_PN_R_SLEW_MAX", "FW_R_TC"],
      ["baseline"],
      "course_hold",
    );
    expect(selected.map((item) => item.id)).toEqual([
      "NPFG_PERIOD",
      "NPFG_DAMPING",
      "FW_R_LIM",
      "FW_PN_R_SLEW_MAX",
    ]);
    expect(controllerParameterCategories(selected, "baseline")).toEqual([{
      id: "px4-course-lateral-guidance",
      label: "PX4 Course / Lateral Guidance",
    }]);
    expect(selected.find((item) => item.id === "FW_R_LIM")).toMatchObject({
      unit: "deg",
      minimum: 0,
      maximum: 75,
      default_value: 20,
      increment: 0.5,
    });
  });

  it("builds all eight Pitch parameters only from exact revision metadata", () => {
    const selected = selectSupportedControllerParameterDefinitions(
      [...pitchHoldDefinitions, ...courseHoldDefinitions],
      [...pitchHoldDefinitions.map((item) => item.id), "NPFG_PERIOD"],
      ["baseline"],
      "pitch_hold",
    );
    expect(selected.map((item) => item.id)).toEqual(
      pitchHoldDefinitions.map((item) => item.id),
    );
    expect(controllerParameterCategories(selected, "baseline")).toEqual([{
      id: "px4-pitch",
      label: "PX4 Pitch",
    }]);
    expect(selected.find((item) => item.id === "FW_P_RMAX_POS")).toMatchObject({
      unit: "deg/s",
      minimum: 0,
      maximum: 180,
      default_value: 14,
      increment: 0.5,
    });
  });

  it("builds all 19 TECS parameters and converts display units from contract metadata", () => {
    const selected = selectSupportedControllerParameterDefinitions(
      [...tecsDefinitions, ...pitchHoldDefinitions],
      [...tecsIds, "FW_P_TC"],
      ["baseline"],
      "tecs",
    );
    expect(selected.map((item) => item.id)).toEqual([...tecsIds]);
    expect(controllerParameterCategories(selected, "baseline").map((item) => item.label)).toEqual([
      "PX4 TECS / Envelope",
      "PX4 TECS / Performance",
      "PX4 TECS / Energy Loop",
      "PX4 TECS / Slew",
      "PX4 TECS / Trim",
    ]);
    const pitchLimit = selected.find((item) => item.id === "FW_P_LIM_MAX")!;
    expect(runtimeParameterViewModel(pitchLimit)).toMatchObject({
      unit: "deg",
      minimum: 0,
      maximum: 45,
      defaultValue: 20,
      step: 0.5,
    });
    const parsed = parseControllerParameterDrafts(
      [pitchLimit], { FW_P_LIM_MAX: "25" }, ["baseline"],
    );
    expect(parsed.errors).toEqual({});
    expect(parsed.values.FW_P_LIM_MAX).toBeCloseTo(25 * Math.PI / 180);
  });
});

describe("ControllerParameterConfigureDialog", () => {
  it("uses a large two-pane category layout and keeps transient decimal drafts editable", () => {
    const onApply = vi.fn();
    render(<ControllerParameterConfigureDialog
      definitions={definitions}
      isOpen
      onApply={onApply}
      onCancel={vi.fn()}
      values={{ FW_RR_P: 0.05, CUSTOM_P_GAIN: 0.1 }}
    />);

    const dialog = screen.getByRole("dialog", { name: "Configure Controller Parameters" });
    const categories = within(dialog).getByLabelText("Parameter categories");
    const body = dialog.querySelector<HTMLElement>(".controller-parameters-configure-body")!;
    const layout = dialog.querySelector<HTMLElement>(".controller-parameters-configure-layout")!;
    expect(dialog).toHaveClass("controller-parameters-configure-dialog");
    expect(window.getComputedStyle(dialog).width).toContain("1100px");
    expect(window.getComputedStyle(body).overflowX).toBe("hidden");
    expect(window.getComputedStyle(body).overflowY).toBe("auto");
    expect(window.getComputedStyle(layout).overflow).toBe("visible");
    expect(within(categories).getByRole("button", { name: "Roll 1" })).toHaveAttribute("aria-pressed", "true");
    expect(within(dialog).getByLabelText("FW_RR_P")).toBeInTheDocument();
    expect(within(dialog).queryByLabelText("CUSTOM_P_GAIN")).not.toBeInTheDocument();

    const input = within(dialog).getByLabelText("FW_RR_P");
    ["", "-", ".", "-0.", "0.", "0.05", "0.1", "0.5", "0.01"].forEach((value) => {
      fireEvent.change(input, { target: { value } });
      expect(input).toHaveValue(value);
    });

    fireEvent.click(within(categories).getByRole("button", { name: "Pitch 1" }));
    expect(within(dialog).queryByLabelText("FW_RR_P")).not.toBeInTheDocument();
    const pitch = within(dialog).getByLabelText("CUSTOM_P_GAIN");
    fireEvent.change(pitch, { target: { value: "0.5" } });
    expect(pitch).toHaveValue("0.5");
    fireEvent.click(within(dialog).getByRole("button", { name: "Apply" }));
    expect(onApply).toHaveBeenCalledWith({ FW_RR_P: 0.01, CUSTOM_P_GAIN: 0.5 });
  });

  it("shows every supported Roll Hold parameter in one Roll category", () => {
    render(<ControllerParameterConfigureDialog
      definitions={rollHoldDefinitions}
      isOpen
      onApply={vi.fn()}
      onCancel={vi.fn()}
      values={{ FW_R_TC: 0.4, FW_RR_P: 0.05, FW_RR_I: 0.1, FW_RR_D: 0, FW_RR_FF: 0.5, FW_RR_IMAX: 0.2 }}
    />);

    const dialog = screen.getByRole("dialog", { name: "Configure Controller Parameters" });
    const categories = within(dialog).getByLabelText("Parameter categories");
    const sidebar = dialog.querySelector<HTMLElement>(".controller-parameter-categories")!;
    const grid = dialog.querySelector<HTMLElement>(".controller-parameter-grid")!;
    const preset = dialog.querySelector<HTMLElement>(".controller-parameter-preset-row")!;
    expect(within(categories).getByRole("button", { name: "Roll 6" })).toHaveAttribute("aria-pressed", "true");
    ["FW_R_TC", "FW_RR_P", "FW_RR_I", "FW_RR_D", "FW_RR_FF", "FW_RR_IMAX"].forEach((id) => {
      expect(within(dialog).getByLabelText(id)).toBeInTheDocument();
    });
    expect(within(categories).queryByRole("button", { name: /Attitude|Rate|Control/ })).not.toBeInTheDocument();
    expect(window.getComputedStyle(sidebar).flexBasis).toBe("200px");
    expect(window.getComputedStyle(sidebar).flexShrink).toBe("0");
    expect(window.getComputedStyle(grid).gridTemplateColumns).toBe("repeat(2, minmax(300px, 1fr))");
    expect(window.getComputedStyle(within(preset).getByText("Runtime Default")).whiteSpace).toBe("nowrap");
  });

  it("validates on blur and Apply without closing, resets defaults, and discards Cancelled drafts", () => {
    const onApply = vi.fn();
    const onCancel = vi.fn();
    const view = render(<ControllerParameterConfigureDialog
      definitions={[definitions[0]]}
      isOpen
      onApply={onApply}
      onCancel={onCancel}
      values={{ FW_RR_P: 0.08 }}
    />);
    let dialog = screen.getByRole("dialog", { name: "Configure Controller Parameters" });
    let input = within(dialog).getByLabelText("FW_RR_P");

    fireEvent.change(input, { target: { value: "-" } });
    expect(input).toHaveValue("-");
    fireEvent.blur(input);
    expect(within(dialog).getByText("FW_RR_P requires a finite value")).toBeInTheDocument();
    fireEvent.click(within(dialog).getByRole("button", { name: "Apply" }));
    expect(onApply).not.toHaveBeenCalled();
    expect(dialog).toBeInTheDocument();

    fireEvent.click(within(dialog).getByRole("button", { name: "Reset defaults" }));
    expect(input).toHaveValue("0.05");
    expect(within(dialog).getByText("ACTIVE · DEFAULT")).toBeInTheDocument();
    fireEvent.change(input, { target: { value: "0.1" } });
    fireEvent.click(within(dialog).getByRole("button", { name: "Cancel" }));
    expect(onCancel).toHaveBeenCalledOnce();

    view.rerender(<ControllerParameterConfigureDialog
      definitions={[definitions[0]]}
      isOpen={false}
      onApply={onApply}
      onCancel={onCancel}
      values={{ FW_RR_P: 0.08 }}
    />);
    view.rerender(<ControllerParameterConfigureDialog
      definitions={[definitions[0]]}
      isOpen
      onApply={onApply}
      onCancel={onCancel}
      values={{ FW_RR_P: 0.08 }}
    />);
    dialog = screen.getByRole("dialog", { name: "Configure Controller Parameters" });
    input = within(dialog).getByLabelText("FW_RR_P");
    expect(input).toHaveValue("0.08");

    fireEvent.change(input, { target: { value: "11" } });
    fireEvent.click(within(dialog).getByRole("button", { name: "Apply" }));
    expect(within(dialog).getByText("FW_RR_P must be at most 10")).toBeInTheDocument();
    expect(onApply).not.toHaveBeenCalled();
  });
});
