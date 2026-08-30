#include "sim/execution/ExecutionVariantResolver.hpp"

#include "sim/gnc/autopilot/AutopilotFactory.hpp"
#include "sim/gnc/autopilot/IAutopilot.hpp"

#include <utility>

namespace sim {
bool ExecutionVariantResolver::Resolve(const ExecutionRequest &request,
    ResolvedExecutionSpec &resolved, std::string &error) {
  std::string validationError;
  if (!ValidateSimulationScenario(request.scenario, &validationError)) {
    error = std::move(validationError);
    return false;
  }
  switch (request.variant) {
  case ExecutionVariant::Baseline:
  case ExecutionVariant::Primary:
    break;
  default:
    error = "Unsupported execution variant: "
            + std::string(ToString(request.variant));
    return false;
  }
  resolved = {
      .scenario = request.scenario,
      .variant = request.variant,
      .source = request.source,
      .parameters = request.parameters,
  };
  error.clear();
  return true;
}

std::unique_ptr<gnc::IAutopilot> ExecutionVariantResolver::CreateAutopilot(
    ExecutionVariant variant) {
  switch (variant) {
  case ExecutionVariant::Baseline:
    return gnc::CreateAutopilot(gnc::AutopilotKind::Baseline);
  case ExecutionVariant::Primary:
    return gnc::CreateAutopilot(gnc::AutopilotKind::Primary);
  }
  return nullptr;
}

std::optional<ExecutionVariant> ExecutionVariantResolver::IdentifyVariant(
    const gnc::IAutopilot &autopilot) {
  const std::optional<gnc::AutopilotKind> kind =
      gnc::IdentifyAutopilotKind(autopilot);
  if (!kind) {
    return std::nullopt;
  }
  switch (*kind) {
  case gnc::AutopilotKind::Baseline:
    return ExecutionVariant::Baseline;
  case gnc::AutopilotKind::Primary:
    return ExecutionVariant::Primary;
  }
  return std::nullopt;
}
} // namespace sim
