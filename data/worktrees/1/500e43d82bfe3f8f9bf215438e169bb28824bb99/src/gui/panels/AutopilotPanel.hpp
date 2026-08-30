#pragma once

#include "gui/architecture/EventSink.hpp"
#include "gui/features/gnc/GNCEvents.hpp"

namespace gui {
struct AutopilotPanelState {
  // Autopilot control selection
  bool rollHold = false;

  // Roll Hold target
  double rollTargetDeg = 0.0;

  // Future P-P controller parameters
  double rollAngleProportionalGain = 0.0;
  double rollRateProportionalGain = 0.0;
  bool rollHoldParametersOpen = true;
};

struct AutopilotPanelProps {
  AutopilotPanelState &state;

  // Roll telemetry and actions
  double currentRollDeg = 0.0;
  double currentRollRateDegPerSec = 0.0;
  double currentAileron = 0.0;
  architecture::EventSink<PrimaryRollHoldValueChanged> events;
};

class AutopilotPanel {
public:
  static void Draw(const AutopilotPanelProps &props);
};
} // namespace gui
