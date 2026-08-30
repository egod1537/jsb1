#pragma once

#include <string_view>

namespace runner {
enum class ExecutionMode {
  Single,
  Compare,
};

std::string_view ToString(ExecutionMode mode);
bool TryParseExecutionMode(std::string_view value, ExecutionMode &mode);
} // namespace runner
