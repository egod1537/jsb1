#include "sim/linearization/LinearizationStateVector.hpp"

#include "sim/Aircraft.hpp"
#include "sim/FDMState.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

namespace {
constexpr double MinimumCosineMagnitude = 1.0e-8;
constexpr double MinimumRadiusFt = 1.0;

std::array<double, 3> ComputeEulerAngleRates(double rollRad, double pitchRad,
    double pRadPerSec, double qRadPerSec, double rRadPerSec) {
  const double sinRoll = std::sin(rollRad);
  const double cosRoll = std::cos(rollRad);
  const double cosPitch = std::cos(pitchRad);
  if (!std::isfinite(cosPitch) || std::abs(cosPitch) < MinimumCosineMagnitude) {
    throw std::domain_error(
        "Cannot extract Euler rates near pitch singularity");
  }

  const double tanPitch = std::tan(pitchRad);
  return {
      pRadPerSec + qRadPerSec * sinRoll * tanPitch
          + rRadPerSec * cosRoll * tanPitch,
      qRadPerSec * cosRoll - rRadPerSec * sinRoll,
      (qRadPerSec * sinRoll + rRadPerSec * cosRoll) / cosPitch,
  };
}
} // namespace

namespace sim {
StateVector ExtractStateVector(const FDMState &state) {
  StateVector vector;
  vector(ToIndex(LinearizationState::U)) = state.state.bodyVelocityFps[0];
  vector(ToIndex(LinearizationState::V)) = state.state.bodyVelocityFps[1];
  vector(ToIndex(LinearizationState::W)) = state.state.bodyVelocityFps[2];
  vector(ToIndex(LinearizationState::P)) =
      state.state.bodyAngularRatesRadPerSec[0];
  vector(ToIndex(LinearizationState::Q)) =
      state.state.bodyAngularRatesRadPerSec[1];
  vector(ToIndex(LinearizationState::R)) =
      state.state.bodyAngularRatesRadPerSec[2];
  vector(ToIndex(LinearizationState::Roll)) = state.state.attitudeRad[0];
  vector(ToIndex(LinearizationState::Pitch)) = state.state.attitudeRad[1];
  vector(ToIndex(LinearizationState::Heading)) = state.state.attitudeRad[2];
  vector(ToIndex(LinearizationState::Latitude)) = state.state.latitudeRad;
  vector(ToIndex(LinearizationState::Longitude)) = state.state.longitudeRad;
  vector(ToIndex(LinearizationState::Altitude)) = state.state.altitudeAslFt;
  return vector;
}

void ApplyStateVector(FDMState &state, const StateVector &vector) {
  state.state.bodyVelocityFps[0] = vector(ToIndex(LinearizationState::U));
  state.state.bodyVelocityFps[1] = vector(ToIndex(LinearizationState::V));
  state.state.bodyVelocityFps[2] = vector(ToIndex(LinearizationState::W));
  state.state.bodyAngularRatesRadPerSec[0] =
      vector(ToIndex(LinearizationState::P));
  state.state.bodyAngularRatesRadPerSec[1] =
      vector(ToIndex(LinearizationState::Q));
  state.state.bodyAngularRatesRadPerSec[2] =
      vector(ToIndex(LinearizationState::R));
  state.state.attitudeRad[0] = vector(ToIndex(LinearizationState::Roll));
  state.state.attitudeRad[1] = vector(ToIndex(LinearizationState::Pitch));
  state.state.attitudeRad[2] = vector(ToIndex(LinearizationState::Heading));
  state.state.latitudeRad = vector(ToIndex(LinearizationState::Latitude));
  state.state.longitudeRad = vector(ToIndex(LinearizationState::Longitude));
  state.state.altitudeAslFt = vector(ToIndex(LinearizationState::Altitude));
}

InputVector ExtractInputVector(const FDMState &state) {
  InputVector vector;
  vector(ToIndex(LinearizationInput::Aileron)) = state.controls.aileronCommand;
  vector(ToIndex(LinearizationInput::Elevator)) =
      state.controls.elevatorCommand;
  vector(ToIndex(LinearizationInput::Rudder)) = state.controls.rudderCommand;
  vector(ToIndex(LinearizationInput::Throttle)) =
      state.controls.throttleCommands.empty()
          ? 0.0
          : state.controls.throttleCommands[0];
  return vector;
}

bool ApplyInputVector(FDMState &state, const InputVector &vector) {
  if (state.controls.throttleCommands.empty()) {
    return false;
  }

  state.controls.aileronCommand = vector(ToIndex(LinearizationInput::Aileron));
  state.controls.elevatorCommand =
      vector(ToIndex(LinearizationInput::Elevator));
  state.controls.rudderCommand = vector(ToIndex(LinearizationInput::Rudder));
  state.controls.throttleCommands[0] =
      vector(ToIndex(LinearizationInput::Throttle));
  return true;
}

StateDerivativeVector ExtractStateDerivativeVector(const Aircraft &aircraft) {
  const jsbsim::Properties &properties = aircraft.GetProperties();
  const std::array<double, 3> eulerRates =
      ComputeEulerAngleRates(properties.Roll().Rad(),
          properties.Pitch().Rad(),
          properties.P().RadPerSec(),
          properties.Q().RadPerSec(),
          properties.R().RadPerSec());

  const double radiusFt = properties.RadiusToVehicle().Ft();
  if (!std::isfinite(radiusFt) || radiusFt < MinimumRadiusFt) {
    throw std::domain_error(
        "Cannot extract geographic rates with an invalid Earth radius");
  }

  const double cosLatitude = std::cos(properties.Latitude().Rad());
  if (!std::isfinite(cosLatitude)
      || std::abs(cosLatitude) < MinimumCosineMagnitude) {
    throw std::domain_error(
        "Cannot extract longitude rate at a geographic pole");
  }

  StateDerivativeVector result;
  result(ToIndex(LinearizationState::U)) = properties.U().DotFps2();
  result(ToIndex(LinearizationState::V)) = properties.V().DotFps2();
  result(ToIndex(LinearizationState::W)) = properties.W().DotFps2();
  result(ToIndex(LinearizationState::P)) = properties.P().DotRadPerSec2();
  result(ToIndex(LinearizationState::Q)) = properties.Q().DotRadPerSec2();
  result(ToIndex(LinearizationState::R)) = properties.R().DotRadPerSec2();
  result(ToIndex(LinearizationState::Roll)) = eulerRates[0];
  result(ToIndex(LinearizationState::Pitch)) = eulerRates[1];
  result(ToIndex(LinearizationState::Heading)) = eulerRates[2];
  result(ToIndex(LinearizationState::Latitude)) =
      properties.NorthVelocity().Fps() / radiusFt;
  result(ToIndex(LinearizationState::Longitude)) =
      properties.EastVelocity().Fps() / (cosLatitude * radiusFt);
  result(ToIndex(LinearizationState::Altitude)) =
      properties.VerticalSpeed().Fps();
  return result;
}
} // namespace sim
