#pragma once

#include "gui/architecture/EventSink.hpp"
#include "gui/features/gnc/GNCEvents.hpp"
#include "gui/panels/AutopilotPanel.hpp"
#include "sim/control/ControlInput.hpp"

namespace gui {
struct ManualControlPanelProps {
  const AutopilotPanelState &autopilotState;
  control::ControlInput input;
  double pitchTrim = 0.0;
  architecture::EventSink<ManualControlChanged> events;
};

class ManualControlPanel {
public:
  static void Draw(const ManualControlPanelProps &props);
};
} // namespace gui
