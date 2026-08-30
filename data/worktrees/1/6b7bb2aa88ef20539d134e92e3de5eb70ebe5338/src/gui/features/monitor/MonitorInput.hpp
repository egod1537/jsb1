#pragma once

#include "sim/linearization/DynamicModeContracts.hpp"

#include <memory>
#include <span>
#include <string_view>

namespace telemetry {
struct TelemetrySnapshot;
}

namespace gui {
// Read-only data published by the application and cached outside Monitor.
// These are snapshot lifetimes, never runtime/simulation object pointers.
struct MonitorDynamicModeInput {
  std::span<const gnc::DynamicModeSnapshot> history;
  std::string_view errorMessage;
  bool available = false;
  bool automaticUpdatesEnabled = false;
  bool updateInProgress = false;
};

struct MonitorInput {
  std::shared_ptr<const telemetry::TelemetrySnapshot> primary;
  std::shared_ptr<const telemetry::TelemetrySnapshot> baseline;
  MonitorDynamicModeInput dynamicModes;
};
} // namespace gui
