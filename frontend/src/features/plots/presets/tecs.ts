import type { PlotPreset } from "../plotTypes";

export const TECS_PRESET: PlotPreset = {
  id: "px4-tecs",
  name: "PX4 TECS",
  description: "Altitude/airspeed energy management, commands, and protection states.",
  applicableScenarioTypes: ["tecs"],
  recommendedLayout: "2x3",
  category: "control",
  plots: [
    {
      id: "tecs-altitude",
      title: "Altitude",
      signals: [
        { name: "tecs.altitude.target" },
        { name: "tecs.altitude.internal_target" },
        { name: "tecs.altitude.actual" },
      ],
    },
    {
      id: "tecs-airspeed",
      title: "Airspeed",
      signals: [
        { name: "tecs.airspeed.target" },
        { name: "tecs.airspeed.actual" },
      ],
    },
    {
      id: "tecs-commands",
      title: "Commands",
      signals: [
        { name: "tecs.pitch_target" },
        { name: "pitch.actual", optional: true },
        { name: "tecs.throttle_target" },
      ],
    },
    {
      id: "tecs-energy",
      title: "Energy Errors",
      signals: [
        { name: "tecs.total_energy_error" },
        { name: "tecs.energy_balance_error" },
      ],
    },
    {
      id: "tecs-vertical-speed",
      title: "Vertical Speed",
      signals: [
        { name: "tecs.vertical_speed.target" },
        { name: "tecs.vertical_speed.actual" },
      ],
    },
    {
      id: "tecs-protection-limits",
      title: "Protection / Limits",
      signals: [
        { name: "tecs.underspeed_active" },
        { name: "tecs.overspeed_active" },
        { name: "tecs.pitch_upper_limited", optional: true },
        { name: "tecs.pitch_lower_limited", optional: true },
        { name: "tecs.throttle_upper_saturated", optional: true },
        { name: "tecs.throttle_lower_saturated", optional: true },
      ],
    },
  ],
};
