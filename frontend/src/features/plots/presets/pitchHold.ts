import type { PlotPreset } from "../plotTypes";

export const PITCH_HOLD_PRESET: PlotPreset = {
  id: "px4-pitch-hold",
  name: "PX4 Pitch Hold",
  description: "Pitch attitude/rate tracking, elevator demand, and stable limiter states.",
  applicableScenarioTypes: ["pitch_hold"],
  recommendedLayout: "2x2",
  category: "control",
  plots: [
    {
      id: "pitch-tracking",
      title: "Pitch Tracking",
      signals: [{ name: "pitch.commanded" }, { name: "pitch.actual" }],
      acceptanceBandSignal: "pitch.commanded",
    },
    {
      id: "pitch-rate-tracking",
      title: "Pitch Rate Tracking",
      signals: [
        { name: "pitch_rate.commanded" },
        { name: "pitch_rate.actual" },
      ],
    },
    {
      id: "pitch-errors",
      title: "Pitch Error / Rate Error",
      signals: [{ name: "pitch.error" }, { name: "pitch_rate.error" }],
    },
    {
      id: "pitch-elevator-limits",
      title: "Elevator / Saturation",
      signals: [
        { name: "elevator" },
        { name: "saturation_positive", optional: true },
        { name: "saturation_negative", optional: true },
        { name: "integrator_limited", optional: true },
      ],
    },
  ],
};

