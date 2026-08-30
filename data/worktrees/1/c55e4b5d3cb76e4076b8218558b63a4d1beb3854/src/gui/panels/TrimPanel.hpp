#pragma once

#include "gui/architecture/EventSink.hpp"
#include "gui/features/gnc/GNCEvents.hpp"
#include "sim/gnc/TrimTypes.hpp"

namespace gui {
struct TrimPanelProps {
  const gnc::TrimRequest &request;
  const gnc::TrimResult &result;
  bool hasResult;
  bool &resultOpen;
  bool &residualOpen;
  bool canRequestTrim;
  architecture::EventSink<TrimRequestValueChanged> valueEvents;
  architecture::EventSink<TrimExecutionRequested> executionEvents;
};

class TrimPanel {
public:
  static void Draw(TrimPanelProps props);
};
} // namespace gui
