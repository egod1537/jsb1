#include "sim/execution/ExecutionVariant.hpp"

namespace sim {
std::string_view ToString(ExecutionVariant variant) {
  switch (variant) {
  case ExecutionVariant::Baseline:
    return "baseline";
  case ExecutionVariant::Primary:
    return "primary";
  }
  return "unknown";
}

bool TryParseExecutionVariant(std::string_view value,
    ExecutionVariant &variant) {
  if (value == "baseline") {
    variant = ExecutionVariant::Baseline;
    return true;
  }
  if (value == "primary") {
    variant = ExecutionVariant::Primary;
    return true;
  }
  return false;
}
} // namespace sim
