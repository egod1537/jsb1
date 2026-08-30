#pragma once

#include "gui/features/autopilot_selector/AutopilotSelectorModel.hpp"

namespace gui {
// Interaction-level event emitted by the selector view.
struct AutopilotSourceSelected {
  AutopilotSelection selection = AutopilotSelection::Primary;
};

// Semantic feature event emitted to an optional parent controller.
struct AutopilotSelectionChanged {
  AutopilotSelection selection = AutopilotSelection::Primary;
};
} // namespace gui
