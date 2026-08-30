#pragma once

#include <memory>
#include <optional>
#include <string_view>

namespace gnc {
class IAutopilot;

enum class AutopilotKind {
  Primary,
  Baseline,
};

std::unique_ptr<IAutopilot> CreateAutopilot(AutopilotKind kind);
const char *ToString(AutopilotKind kind);
bool TryParseAutopilotKind(std::string_view value, AutopilotKind &kind);
std::optional<AutopilotKind> IdentifyAutopilotKind(const IAutopilot &autopilot);
} // namespace gnc
