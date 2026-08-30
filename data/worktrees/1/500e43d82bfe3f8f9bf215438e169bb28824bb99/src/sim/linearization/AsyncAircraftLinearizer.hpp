#pragma once

#include "sim/FDMState.hpp"
#include "sim/InitialCondition.hpp"
#include "sim/SimulationConfig.h"
#include "sim/linearization/LinearizationResult.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace sim {
class AsyncAircraftLinearizer {
public:
  struct Completion {
    std::uint64_t generation{};
    std::optional<gnc::LinearizationResult> linearization;
    std::string errorMessage;
  };

  AsyncAircraftLinearizer();
  ~AsyncAircraftLinearizer();

  AsyncAircraftLinearizer(const AsyncAircraftLinearizer &other) = delete;
  AsyncAircraftLinearizer &operator=(
      const AsyncAircraftLinearizer &other) = delete;

  bool Submit(std::uint64_t generation, const SimulationConfig &config,
      const InitialCondition &initialCondition, FDMState sourceState);
  bool IsBusy() const;
  std::optional<Completion> TakeCompletion();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace sim
