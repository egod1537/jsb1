#include "ExecutionMode.hpp"

namespace runner {
std::string_view ToString(ExecutionMode mode) {
  switch (mode) {
  case ExecutionMode::Single:
    return "single";
  case ExecutionMode::Compare:
    return "compare";
  }
  return "single";
}

bool TryParseExecutionMode(std::string_view value, ExecutionMode &mode) {
  if (value == "single") {
    mode = ExecutionMode::Single;
    return true;
  }
  if (value == "compare") {
    mode = ExecutionMode::Compare;
    return true;
  }
  return false;
}
} // namespace runner
