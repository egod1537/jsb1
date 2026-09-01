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
  { id: "FW_R_TC", display_name: "Roll Time Constant", category: "Attitude", unit: "s", default_value: 0.4, minimum: 0.2, maximum: 1, variants: ["baseline"] },
  { ...definitions[0], category: "Rate" },
  { id: "FW_RR_I", display_name: "Roll Rate I", group: "Rate", default_value: 0.1, minimum: 0, maximum: 10, variants: ["baseline"] },
  { id: "FW_RR_D", display_name: "Roll Rate D", group: "Rate", default_value: 0, minimum: 0, maximum: 10, variants: ["baseline"] },
  { id: "FW_RR_FF", display_name: "Roll Rate Feed Forward", group: "Rate", default_value: 0.5, minimum: 0, maximum: 10, variants: ["baseline"] },
  { id: "FW_RR_IMAX", display_name: "Roll Integrator Limit", group: "Control", default_value: 0.2, minimum: 0, maximum: 1, variants: ["baseline"] },
];

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
    expect(window.getComputedStyle(within(preset).getByText("PX4 Default")).whiteSpace).toBe("nowrap");
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
