#pragma once

#include "sim/execution/ExecutionVariant.hpp"
#include "sim/scenario/SimulationScenario.hpp"

#include <string>

namespace sim {
struct ScenarioSource {
  std::string file;
  std::string digestSha256;
  bool operator==(const ScenarioSource &) const = default;
};

struct ExecutionRequest {
  SimulationScenario scenario;
  ExecutionVariant variant = ExecutionVariant::Primary;
  ScenarioSource source;
};

struct ResolvedExecutionSpec {
  SimulationScenario scenario;
  ExecutionVariant variant = ExecutionVariant::Primary;
  ScenarioSource source;
};
} // namespace sim
