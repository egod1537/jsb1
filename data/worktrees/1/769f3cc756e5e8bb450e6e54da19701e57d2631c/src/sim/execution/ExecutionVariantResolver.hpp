#pragma once

#include "sim/execution/ExecutionRequest.hpp"

#include <memory>
#include <optional>
#include <string>

namespace gnc {
class IAutopilot;
}

namespace sim {
class ExecutionVariantResolver {
public:
  // Request resolution
  static bool Resolve(const ExecutionRequest &request,
      ResolvedExecutionSpec &resolved, std::string &error);

  // Autopilot implementation mapping
  static std::unique_ptr<gnc::IAutopilot> CreateAutopilot(
      ExecutionVariant variant);
  static std::optional<ExecutionVariant> IdentifyVariant(
      const gnc::IAutopilot &autopilot);
};
} // namespace sim
