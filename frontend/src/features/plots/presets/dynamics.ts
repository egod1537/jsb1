import type { PlotPreset } from "../plotTypes";
import { plotFromTemplate } from "../plotTemplates";

// This preset intentionally contains only signals present in JSB0 main's
// current telemetry catalog. Pitch/yaw/aerodynamic signals can be added when
// the runtime contract exposes them.
export const DYNAMICS_PRESET: PlotPreset = {
  id: "dynamics",
  name: "Dynamics",
  description: "General aircraft response using the dynamics channels available in this runtime.",
  recommendedLayout: "2x2",
  category: "general",
  plots: [
    plotFromTemplate("attitude"),
    plotFromTemplate("body-rates", "angular-rates", "Angular Rates"),
    plotFromTemplate("aileron", "control-surfaces", "Control Surfaces"),
  ],
};
