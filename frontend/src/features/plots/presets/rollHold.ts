import type { PlotPreset } from "../plotTypes";
import { plotFromTemplate } from "../plotTemplates";

export const ROLL_HOLD_PRESET: PlotPreset = {
  id: "roll-hold",
  name: "Roll Hold",
  description: "Command, aircraft roll response, body roll rate, and control effort.",
  applicableScenarioTypes: ["roll_hold"],
  recommendedLayout: "2x2",
  category: "control",
  plots: [
    plotFromTemplate("roll-tracking", "roll-angle", "Roll Angle"),
    plotFromTemplate("roll-rate-tracking", "roll-rate", "Roll Rate"),
    plotFromTemplate("aileron"),
  ],
};
