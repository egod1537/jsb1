import { afterEach, describe, expect, it, vi } from "vitest";
import { api } from "../../api/client";
import { createRunVariantDataSource } from "./runVariantDataSource";

afterEach(() => vi.restoreAllMocks());

describe("intra-Run variant telemetry", () => {
  it("deduplicates identical commands while keeping variant responses separate", async () => {
    vi.spyOn(api, "availableSignals").mockResolvedValue({ signals: [
      { name: "commanded_roll", display_name: "Commanded Roll", unit: "deg" },
      { name: "roll", display_name: "Roll", unit: "deg" },
    ] });
    vi.spyOn(api, "signals").mockImplementation(async (_id, _signals, _points, variant) => ({
      time: [0, 1, 2],
      series: {
        commanded_roll: [0, 5, 5],
        roll: variant === "baseline" ? [0, 2, 4] : [0, 3, 5],
      },
      units: { commanded_roll: "deg", roll: "deg" },
      source_points: 3,
      returned_points: 3,
    }));
    const source = createRunVariantDataSource(42, ["baseline", "primary"], "overlay");

    const telemetry = await source.loadSignals(["commanded_roll", "roll"]);
    const command = source.resolveSignal?.("commanded_roll", telemetry) ?? [];
    const roll = source.resolveSignal?.("roll", telemetry) ?? [];

    expect(command).toHaveLength(1);
    expect(command[0].name).toBe("Commanded Roll");
    expect(roll.map((item) => item.name)).toEqual([
      "baseline / Roll",
      "primary / Roll",
    ]);
    expect(telemetry.series["baseline/roll"]).not.toEqual(telemetry.series["primary/roll"]);
  });
});
