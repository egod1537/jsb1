#pragma once

#include "sim/execution/ExecutionVariant.hpp"
#include "sim/scenario/SimulationScenario.hpp"

#include <map>
#include <string>

namespace sim {
struct ScenarioSource {
  std::string file;
  std::string digestSha256;
  bool operator==(const ScenarioSource &) const = default;
};

using ExecutionParameterSet = std::map<std::string, double>;

struct ExecutionRequest {
  SimulationScenario scenario;
  ExecutionVariant variant = ExecutionVariant::Primary;
  ScenarioSource source;
  ExecutionParameterSet parameters;
};

struct ResolvedExecutionSpec {
  SimulationScenario scenario;
  ExecutionVariant variant = ExecutionVariant::Primary;
  ScenarioSource source;
  ExecutionParameterSet parameters;
};

struct ComparisonExecutionRequest {
  SimulationScenario scenario;
  ScenarioSource source;
  ExecutionParameterSet baselineParameters;
  ExecutionParameterSet primaryParameters;
};
} // namespace sim
