#pragma once

#include "gui/architecture/EventSink.hpp"
#include "gui/features/autopilot_selector/AutopilotSelectorEvents.hpp"

namespace gui {
struct AutopilotSelectorProps {
  bool baselineAvailable = false;
};

class AutopilotSelectorView {
public:
  void Render(const AutopilotViewState &model,
      const AutopilotSelectorProps &props,
      architecture::EventSink<AutopilotSourceSelected> events) const;
};
} // namespace gui
