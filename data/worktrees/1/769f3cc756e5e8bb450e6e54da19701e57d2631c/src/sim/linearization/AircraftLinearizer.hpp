#pragma once

#include "sim/Aircraft.hpp"
#include "sim/FDMState.hpp"

namespace gnc {
struct LinearizationResult;
}

namespace sim {
class AircraftLinearizer {
public:
  bool Initialize(const SimulationConfig &config,
      const InitialCondition &initialCondition);
  gnc::LinearizationResult Linearize(const FDMState &sourceState);

private:
  void SyncFrom(const FDMState &sourceState);

  Aircraft aircraft_;
  bool initialized_ = false;
};
} // namespace sim
