import type { PlotPreset } from "../plotTypes";

/** Empty composition used for session-local custom workspaces. */
export const CUSTOM_PRESET: PlotPreset = {
  id: "custom",
  name: "Raw Signals",
  description: "Start with an empty workspace and compose plots from this run's channels.",
  category: "custom",
  plots: [],
};
