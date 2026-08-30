#include "sim/FDMState.hpp"
#include "sim/linearization/LinearizationPerturbation.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
constexpr double Tolerance = 1.0e-12;

void RequireNear(double actual, double expected, const std::string &message) {
  if (std::fabs(actual - expected) > Tolerance) {
    throw std::runtime_error(message);
  }
}

void TestStatePerturbations() {
  sim::FDMState state{};
  state.state.bodyVelocityFps = {10.0, 20.0, 30.0};
  state.state.bodyAngularRatesRadPerSec = {0.1, 0.2, 0.3};
  state.state.attitudeRad = {0.4, 0.5, 0.6};
  state.state.latitudeRad = 0.7;
  state.state.longitudeRad = 0.8;
  state.state.altitudeAslFt = 1'000.0;

  sim::ApplyPerturbation(state, {sim::LinearizationState::U, 1.0});
  sim::ApplyPerturbation(state, {sim::LinearizationState::V, 2.0});
  sim::ApplyPerturbation(state, {sim::LinearizationState::W, 3.0});
  sim::ApplyPerturbation(state, {sim::LinearizationState::P, 0.01});
  sim::ApplyPerturbation(state, {sim::LinearizationState::Q, 0.02});
  sim::ApplyPerturbation(state, {sim::LinearizationState::R, 0.03});
  sim::ApplyPerturbation(state, {sim::LinearizationState::Roll, 0.04});
  sim::ApplyPerturbation(state, {sim::LinearizationState::Pitch, 0.05});
  sim::ApplyPerturbation(state, {sim::LinearizationState::Heading, 0.06});
  sim::ApplyPerturbation(state, {sim::LinearizationState::Latitude, 0.07});
  sim::ApplyPerturbation(state, {sim::LinearizationState::Longitude, 0.08});
  sim::ApplyPerturbation(state, {sim::LinearizationState::Altitude, 100.0});

  RequireNear(state.state.bodyVelocityFps[0], 11.0, "U perturbation failed");
  RequireNear(state.state.bodyVelocityFps[1], 22.0, "V perturbation failed");
  RequireNear(state.state.bodyVelocityFps[2], 33.0, "W perturbation failed");
  RequireNear(state.state.bodyAngularRatesRadPerSec[0],
      0.11,
      "P perturbation failed");
  RequireNear(state.state.bodyAngularRatesRadPerSec[1],
      0.22,
      "Q perturbation failed");
  RequireNear(state.state.bodyAngularRatesRadPerSec[2],
      0.33,
      "R perturbation failed");
  RequireNear(state.state.attitudeRad[0], 0.44, "Roll perturbation failed");
  RequireNear(state.state.attitudeRad[1], 0.55, "Pitch perturbation failed");
  RequireNear(state.state.attitudeRad[2], 0.66, "Heading perturbation failed");
  RequireNear(state.state.latitudeRad, 0.77, "Latitude perturbation failed");
  RequireNear(state.state.longitudeRad, 0.88, "Longitude perturbation failed");
  RequireNear(state.state.altitudeAslFt,
      1'100.0,
      "Altitude perturbation failed");
}

void TestInputPerturbations() {
  sim::FDMState state{};
  state.controls.aileronCommand = 0.1;
  state.controls.elevatorCommand = 0.2;
  state.controls.rudderCommand = 0.3;
  state.controls.throttleCommands = {0.4, 0.5};

  sim::ApplyPerturbation(state, {sim::LinearizationInput::Aileron, 0.01});
  sim::ApplyPerturbation(state, {sim::LinearizationInput::Elevator, 0.02});
  sim::ApplyPerturbation(state, {sim::LinearizationInput::Rudder, 0.03});
  sim::ApplyPerturbation(state, {sim::LinearizationInput::Throttle, 0.04});

  RequireNear(state.controls.aileronCommand,
      0.11,
      "Aileron perturbation failed");
  RequireNear(state.controls.elevatorCommand,
      0.22,
      "Elevator perturbation failed");
  RequireNear(state.controls.rudderCommand, 0.33, "Rudder perturbation failed");
  RequireNear(state.controls.throttleCommands[0],
      0.44,
      "Throttle perturbation failed");
  RequireNear(state.controls.throttleCommands[1],
      0.5,
      "Throttle perturbation changed another engine");
}
} // namespace

int main() {
  try {
    TestStatePerturbations();
    TestInputPerturbations();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }

  return 0;
}
