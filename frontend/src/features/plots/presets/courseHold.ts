import type { PlotPreset } from "../plotTypes";

export const COURSE_HOLD_PRESET: PlotPreset = {
  id: "px4-course-hold",
  name: "PX4 Course Hold",
  description: "Wrap-aware course tracking, roll demand, and lateral-guidance state.",
  applicableScenarioTypes: ["course_hold"],
  recommendedLayout: "2x2",
  category: "control",
  plots: [
    {
      id: "course-tracking",
      title: "Course Tracking",
      signals: [{ name: "course.commanded" }, { name: "course.actual" }],
      angularAware: true,
      acceptanceBandSignal: "course.commanded",
    },
    {
      id: "course-error",
      title: "Course Error",
      signals: [{ name: "course.error" }],
    },
    {
      id: "course-roll-tracking",
      title: "Roll Setpoint Tracking",
      signals: [{ name: "roll_setpoint" }, { name: "roll" }],
    },
    {
      id: "course-guidance",
      title: "Ground Speed / Guidance",
      signals: [
        { name: "ground_speed" },
        { name: "raw_lateral_acceleration", optional: true },
        { name: "limited_lateral_acceleration", optional: true },
      ],
    },
  ],
};
