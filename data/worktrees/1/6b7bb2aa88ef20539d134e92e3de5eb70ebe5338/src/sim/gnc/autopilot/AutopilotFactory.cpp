#include "sim/gnc/autopilot/AutopilotFactory.hpp"

#include "sim/gnc/autopilot/IAutopilot.hpp"
#include "sim/gnc/autopilot/MyAutopilot.hpp"
#include "sim/gnc/autopilot/PX4Autopilot.hpp"

namespace gnc {
std::unique_ptr<IAutopilot> CreateAutopilot(AutopilotKind kind) {
  switch (kind) {
  case AutopilotKind::Primary:
    return std::make_unique<MyAutopilot>();
  case AutopilotKind::Baseline:
    return std::make_unique<PX4Autopilot>();
  }
  return nullptr;
}

const char *ToString(AutopilotKind kind) {
  switch (kind) {
  case AutopilotKind::Primary:
    return "primary";
  case AutopilotKind::Baseline:
    return "baseline";
  }
  return "unknown";
}

bool TryParseAutopilotKind(std::string_view value, AutopilotKind &kind) {
  if (value == "primary") {
    kind = AutopilotKind::Primary;
    return true;
  }
  if (value == "baseline") {
    kind = AutopilotKind::Baseline;
    return true;
  }
  return false;
}

std::optional<AutopilotKind> IdentifyAutopilotKind(
    const IAutopilot &autopilot) {
  if (dynamic_cast<const MyAutopilot *>(&autopilot) != nullptr) {
    return AutopilotKind::Primary;
  }
  if (dynamic_cast<const PX4Autopilot *>(&autopilot) != nullptr) {
    return AutopilotKind::Baseline;
  }
  return std::nullopt;
}
} // namespace gnc
