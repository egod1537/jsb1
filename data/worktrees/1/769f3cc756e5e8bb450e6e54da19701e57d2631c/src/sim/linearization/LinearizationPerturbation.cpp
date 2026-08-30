#include "sim/linearization/LinearizationPerturbation.hpp"

#include "sim/FDMState.hpp"

#include <cassert>

namespace sim {
void ApplyPerturbation(FDMState &state, const StatePerturbation &perturbation) {
  switch (perturbation.variable) {
  case LinearizationState::U:
    state.state.bodyVelocityFps[0] += perturbation.amount;
    return;
  case LinearizationState::V:
    state.state.bodyVelocityFps[1] += perturbation.amount;
    return;
  case LinearizationState::W:
    state.state.bodyVelocityFps[2] += perturbation.amount;
    return;
  case LinearizationState::P:
    state.state.bodyAngularRatesRadPerSec[0] += perturbation.amount;
    return;
  case LinearizationState::Q:
    state.state.bodyAngularRatesRadPerSec[1] += perturbation.amount;
    return;
  case LinearizationState::R:
    state.state.bodyAngularRatesRadPerSec[2] += perturbation.amount;
    return;
  case LinearizationState::Roll:
    state.state.attitudeRad[0] += perturbation.amount;
    return;
  case LinearizationState::Pitch:
    state.state.attitudeRad[1] += perturbation.amount;
    return;
  case LinearizationState::Heading:
    state.state.attitudeRad[2] += perturbation.amount;
    return;
  case LinearizationState::Latitude:
    state.state.latitudeRad += perturbation.amount;
    return;
  case LinearizationState::Longitude:
    state.state.longitudeRad += perturbation.amount;
    return;
  case LinearizationState::Altitude:
    state.state.altitudeAslFt += perturbation.amount;
    return;
  }

  assert(false && "Unknown linearization state variable");
}

void ApplyPerturbation(FDMState &state, const InputPerturbation &perturbation) {
  switch (perturbation.variable) {
  case LinearizationInput::Aileron:
    state.controls.aileronCommand += perturbation.amount;
    return;
  case LinearizationInput::Elevator:
    state.controls.elevatorCommand += perturbation.amount;
    return;
  case LinearizationInput::Rudder:
    state.controls.rudderCommand += perturbation.amount;
    return;
  case LinearizationInput::Throttle:
    assert(!state.controls.throttleCommands.empty()
           && "Cannot perturb throttle without an engine command");
    if (state.controls.throttleCommands.empty()) {
      return;
    }
    state.controls.throttleCommands[0] += perturbation.amount;
    return;
  }

  assert(false && "Unknown linearization input variable");
}
} // namespace sim
