#include "AircraftLinearizer.hpp"
#include "sim/linearization/LinearizationResult.hpp"

#include <stdexcept>

namespace sim {
bool AircraftLinearizer::Initialize(const SimulationConfig &config,
    const InitialCondition &initialCondition) {
  initialized_ = aircraft_.Initialize(config, initialCondition);
  if (!initialized_) {
    return false;
  }

  aircraft_.SetIntegrationSuspended(true);
  return true;
}

gnc::LinearizationResult AircraftLinearizer::Linearize(
    const FDMState &sourceState) {
  if (!initialized_) {
    throw std::logic_error("Aircraft linearizer is not initialized");
  }

  SyncFrom(sourceState);
  return aircraft_.ComputeLinearization();
}

void AircraftLinearizer::SyncFrom(const FDMState &sourceState) {
  aircraft_.ApplyFDMState(sourceState);
}
} // namespace sim
