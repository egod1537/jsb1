import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { ControllerParameterEditor, controllerParameterErrors, controllerParameterOverrides, defaultControllerParameterValues } from "./ControllerParameterEditor";

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
}];

afterEach(cleanup);

describe("ControllerParameterEditor", () => {
  it("shows Runtime metadata and edits canonical values", () => {
    const onChange = vi.fn();
    render(<ControllerParameterEditor definitions={definitions} onChange={onChange} values={{ FW_RR_P: 0.05 }} variant="baseline" />);
    expect(screen.getByText("Roll Rate P")).toBeInTheDocument();
    expect(screen.getByText("FW_RR_P")).toBeInTheDocument();
    expect(screen.getByText("%/rad/s")).toBeInTheDocument();
    fireEvent.change(screen.getByLabelText("FW_RR_P"), { target: { value: "0.08" } });
    expect(onChange).toHaveBeenCalledWith({ FW_RR_P: 0.08 });
  });

  it("resolves defaults, overrides, bounds, and variant applicability", () => {
    expect(defaultControllerParameterValues(definitions, "baseline")).toEqual({ FW_RR_P: 0.05 });
    expect(controllerParameterOverrides(definitions, { FW_RR_P: 0.08 }, "baseline")).toEqual({ FW_RR_P: 0.08 });
    expect(controllerParameterErrors(definitions, { FW_RR_P: 11 }, "baseline")).toEqual(["FW_RR_P must be at most 10"]);
    expect(defaultControllerParameterValues(definitions, "primary")).toEqual({});
  });
});
