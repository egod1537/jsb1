#include "sim/Aircraft.hpp"
#include "sim/FDMState.hpp"
#include "sim/linearization/LinearizationStateVector.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
constexpr double Tolerance = 1.0e-12;

void Require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void RequireNear(double actual, double expected, const std::string &message) {
  if (std::fabs(actual - expected) > Tolerance) {
    throw std::runtime_error(message);
  }
}

sim::FDMState MakePopulatedState() {
  sim::FDMState state{};
  state.state.bodyVelocityFps = {101.0, 102.0, 103.0};
  state.state.bodyAngularRatesRadPerSec = {0.11, 0.12, 0.13};
  state.state.attitudeRad = {0.21, 0.22, 0.23};
  state.state.latitudeRad = 0.31;
  state.state.longitudeRad = 0.32;
  state.state.altitudeAslFt = 4'500.0;
  state.controls.aileronCommand = -0.1;
  state.controls.elevatorCommand = 0.2;
  state.controls.rudderCommand = -0.3;
  state.controls.throttleCommands = {0.65, 0.75};
  return state;
}

void RequireStateFieldsMatch(const sim::FDMState &actual,
    const sim::FDMState &expected) {
  for (std::size_t index = 0; index < 3; ++index) {
    RequireNear(actual.state.bodyVelocityFps[index],
        expected.state.bodyVelocityFps[index],
        "Body velocity mismatch");
    RequireNear(actual.state.bodyAngularRatesRadPerSec[index],
        expected.state.bodyAngularRatesRadPerSec[index],
        "Body angular rate mismatch");
    RequireNear(actual.state.attitudeRad[index],
        expected.state.attitudeRad[index],
        "Attitude mismatch");
  }
  RequireNear(actual.state.latitudeRad,
      expected.state.latitudeRad,
      "Latitude mismatch");
  RequireNear(actual.state.longitudeRad,
      expected.state.longitudeRad,
      "Longitude mismatch");
  RequireNear(actual.state.altitudeAslFt,
      expected.state.altitudeAslFt,
      "Altitude mismatch");
}

void RequireInputFieldsMatch(const sim::FDMState &actual,
    const sim::FDMState &expected) {
  RequireNear(actual.controls.aileronCommand,
      expected.controls.aileronCommand,
      "Aileron mismatch");
  RequireNear(actual.controls.elevatorCommand,
      expected.controls.elevatorCommand,
      "Elevator mismatch");
  RequireNear(actual.controls.rudderCommand,
      expected.controls.rudderCommand,
      "Rudder mismatch");
  RequireNear(actual.controls.throttleCommands[0],
      expected.controls.throttleCommands[0],
      "Throttle mismatch");
}

void TestExtractStateVector() {
  const sim::FDMState state = MakePopulatedState();
  const sim::StateVector vector = sim::ExtractStateVector(state);

  RequireNear(vector(sim::ToIndex(sim::LinearizationState::U)),
      101.0,
      "U extraction failed");
  RequireNear(vector(sim::ToIndex(sim::LinearizationState::V)),
      102.0,
      "V extraction failed");
  RequireNear(vector(sim::ToIndex(sim::LinearizationState::W)),
      103.0,
      "W extraction failed");
  RequireNear(vector(sim::ToIndex(sim::LinearizationState::P)),
      0.11,
      "P extraction failed");
  RequireNear(vector(sim::ToIndex(sim::LinearizationState::Q)),
      0.12,
      "Q extraction failed");
  RequireNear(vector(sim::ToIndex(sim::LinearizationState::R)),
      0.13,
      "R extraction failed");
  RequireNear(vector(sim::ToIndex(sim::LinearizationState::Roll)),
      0.21,
      "Roll extraction failed");
  RequireNear(vector(sim::ToIndex(sim::LinearizationState::Pitch)),
      0.22,
      "Pitch extraction failed");
  RequireNear(vector(sim::ToIndex(sim::LinearizationState::Heading)),
      0.23,
      "Heading extraction failed");
  RequireNear(vector(sim::ToIndex(sim::LinearizationState::Latitude)),
      0.31,
      "Latitude extraction failed");
  RequireNear(vector(sim::ToIndex(sim::LinearizationState::Longitude)),
      0.32,
      "Longitude extraction failed");
  RequireNear(vector(sim::ToIndex(sim::LinearizationState::Altitude)),
      4'500.0,
      "Altitude extraction failed");
}

void TestApplyStateVector() {
  sim::StateVector vector;
  for (Eigen::Index index = 0; index < vector.size(); ++index) {
    vector(index) = 10.0 + static_cast<double>(index);
  }

  sim::FDMState state{};
  sim::ApplyStateVector(state, vector);

  RequireNear(state.state.bodyVelocityFps[0], 10.0, "U application failed");
  RequireNear(state.state.bodyVelocityFps[1], 11.0, "V application failed");
  RequireNear(state.state.bodyVelocityFps[2], 12.0, "W application failed");
  RequireNear(state.state.bodyAngularRatesRadPerSec[0],
      13.0,
      "P application failed");
  RequireNear(state.state.bodyAngularRatesRadPerSec[1],
      14.0,
      "Q application failed");
  RequireNear(state.state.bodyAngularRatesRadPerSec[2],
      15.0,
      "R application failed");
  RequireNear(state.state.attitudeRad[0], 16.0, "Roll application failed");
  RequireNear(state.state.attitudeRad[1], 17.0, "Pitch application failed");
  RequireNear(state.state.attitudeRad[2], 18.0, "Heading application failed");
  RequireNear(state.state.latitudeRad, 19.0, "Latitude application failed");
  RequireNear(state.state.longitudeRad, 20.0, "Longitude application failed");
  RequireNear(state.state.altitudeAslFt, 21.0, "Altitude application failed");
}

void TestExtractInputVector() {
  const sim::FDMState state = MakePopulatedState();
  const sim::InputVector vector = sim::ExtractInputVector(state);

  RequireNear(vector(sim::ToIndex(sim::LinearizationInput::Aileron)),
      -0.1,
      "Aileron extraction failed");
  RequireNear(vector(sim::ToIndex(sim::LinearizationInput::Elevator)),
      0.2,
      "Elevator extraction failed");
  RequireNear(vector(sim::ToIndex(sim::LinearizationInput::Rudder)),
      -0.3,
      "Rudder extraction failed");
  RequireNear(vector(sim::ToIndex(sim::LinearizationInput::Throttle)),
      0.65,
      "Throttle extraction failed");
}

void TestExtractInputVectorWithoutThrottle() {
  sim::FDMState state = MakePopulatedState();
  state.controls.throttleCommands.clear();

  const sim::InputVector vector = sim::ExtractInputVector(state);

  RequireNear(vector(sim::ToIndex(sim::LinearizationInput::Throttle)),
      0.0,
      "Empty throttle extraction did not use the safe default");
}

void TestApplyInputVector() {
  sim::InputVector vector;
  vector(sim::ToIndex(sim::LinearizationInput::Aileron)) = 0.1;
  vector(sim::ToIndex(sim::LinearizationInput::Elevator)) = 0.2;
  vector(sim::ToIndex(sim::LinearizationInput::Rudder)) = 0.3;
  vector(sim::ToIndex(sim::LinearizationInput::Throttle)) = 0.4;

  sim::FDMState state = MakePopulatedState();
  Require(sim::ApplyInputVector(state, vector),
      "Input vector application failed");
  RequireNear(state.controls.aileronCommand, 0.1, "Aileron application failed");
  RequireNear(state.controls.elevatorCommand,
      0.2,
      "Elevator application failed");
  RequireNear(state.controls.rudderCommand, 0.3, "Rudder application failed");
  RequireNear(state.controls.throttleCommands[0],
      0.4,
      "Throttle application failed");
  RequireNear(state.controls.throttleCommands[1],
      0.75,
      "Input application changed another engine");
}

void TestApplyInputVectorWithoutThrottle() {
  sim::FDMState state = MakePopulatedState();
  state.controls.throttleCommands.clear();
  const sim::FDMControlState controlsBefore = state.controls;

  sim::InputVector vector = sim::InputVector::Constant(0.9);
  Require(!sim::ApplyInputVector(state, vector),
      "Empty throttle application unexpectedly succeeded");
  Require(state.controls.throttleCommands.empty(),
      "Empty throttle application resized the command vector");
  RequireNear(state.controls.aileronCommand,
      controlsBefore.aileronCommand,
      "Failed input application changed aileron");
  RequireNear(state.controls.elevatorCommand,
      controlsBefore.elevatorCommand,
      "Failed input application changed elevator");
  RequireNear(state.controls.rudderCommand,
      controlsBefore.rudderCommand,
      "Failed input application changed rudder");
}

void TestStateVectorRoundTrip() {
  const sim::FDMState source = MakePopulatedState();
  sim::FDMState destination{};

  sim::ApplyStateVector(destination, sim::ExtractStateVector(source));

  RequireStateFieldsMatch(destination, source);
}

void TestInputVectorRoundTrip() {
  const sim::FDMState source = MakePopulatedState();
  sim::FDMState destination{};
  destination.controls.throttleCommands = {0.0, 0.8};

  Require(sim::ApplyInputVector(destination, sim::ExtractInputVector(source)),
      "Input vector round trip failed");
  RequireInputFieldsMatch(destination, source);
  RequireNear(destination.controls.throttleCommands[1],
      0.8,
      "Input round trip changed another engine");
}

void SetAttitudeAndBodyRates(sim::Aircraft &aircraft, double rollRad,
    double pitchRad, double pRadPerSec, double qRadPerSec, double rRadPerSec) {
  sim::FDMState state = aircraft.ExtractFDMState(sim::FDMStateFlags::All);
  state.state.attitudeRad[0] = rollRad;
  state.state.attitudeRad[1] = pitchRad;
  state.state.bodyAngularRatesRadPerSec = {
      pRadPerSec,
      qRadPerSec,
      rRadPerSec,
  };
  aircraft.ApplyFDMState(state);
}

void RequireEulerRates(const sim::Aircraft &aircraft, double expectedRollDot,
    double expectedPitchDot, double expectedHeadingDot,
    const std::string &message) {
  const sim::StateDerivativeVector derivative =
      sim::ExtractStateDerivativeVector(aircraft);
  RequireNear(derivative(sim::ToIndex(sim::LinearizationState::Roll)),
      expectedRollDot,
      message + " roll rate");
  RequireNear(derivative(sim::ToIndex(sim::LinearizationState::Pitch)),
      expectedPitchDot,
      message + " pitch rate");
  RequireNear(derivative(sim::ToIndex(sim::LinearizationState::Heading)),
      expectedHeadingDot,
      message + " heading rate");
}

void TestStateDerivativeVectorOrdering(const sim::Aircraft &aircraft) {
  const sim::StateDerivativeVector derivative =
      sim::ExtractStateDerivativeVector(aircraft);
  const sim::jsbsim::Properties &properties = aircraft.GetProperties();
  const double radiusFt = properties.RadiusToVehicle().Ft();

  RequireNear(derivative(sim::ToIndex(sim::LinearizationState::U)),
      properties.U().DotFps2(),
      "UDot ordering failed");
  RequireNear(derivative(sim::ToIndex(sim::LinearizationState::V)),
      properties.V().DotFps2(),
      "VDot ordering failed");
  RequireNear(derivative(sim::ToIndex(sim::LinearizationState::W)),
      properties.W().DotFps2(),
      "WDot ordering failed");
  RequireNear(derivative(sim::ToIndex(sim::LinearizationState::P)),
      properties.P().DotRadPerSec2(),
      "PDot ordering failed");
  RequireNear(derivative(sim::ToIndex(sim::LinearizationState::Q)),
      properties.Q().DotRadPerSec2(),
      "QDot ordering failed");
  RequireNear(derivative(sim::ToIndex(sim::LinearizationState::R)),
      properties.R().DotRadPerSec2(),
      "RDot ordering failed");
  RequireNear(derivative(sim::ToIndex(sim::LinearizationState::Latitude)),
      properties.NorthVelocity().Fps() / radiusFt,
      "LatitudeDot ordering failed");
  RequireNear(derivative(sim::ToIndex(sim::LinearizationState::Longitude)),
      properties.EastVelocity().Fps()
          / (std::cos(properties.Latitude().Rad()) * radiusFt),
      "LongitudeDot ordering failed");
  RequireNear(derivative(sim::ToIndex(sim::LinearizationState::Altitude)),
      properties.VerticalSpeed().Fps(),
      "AltitudeDot ordering failed");
}

void TestEulerRateKinematics(sim::Aircraft &aircraft) {
  SetAttitudeAndBodyRates(aircraft, 0.0, 0.0, 0.0, 0.0, 0.0);
  RequireEulerRates(aircraft, 0.0, 0.0, 0.0, "Zero body rates");

  SetAttitudeAndBodyRates(aircraft, 0.0, 0.0, 0.4, 0.0, 0.0);
  RequireEulerRates(aircraft, 0.4, 0.0, 0.0, "P-only motion");

  SetAttitudeAndBodyRates(aircraft, 0.0, 0.0, 0.0, 0.5, 0.0);
  RequireEulerRates(aircraft, 0.0, 0.5, 0.0, "Q-only motion");

  SetAttitudeAndBodyRates(aircraft, 0.0, 0.0, 0.0, 0.0, 0.6);
  RequireEulerRates(aircraft, 0.0, 0.0, 0.6, "R-only motion");

  constexpr double RollRad = 0.3;
  constexpr double PitchRad = -0.2;
  constexpr double P = 0.4;
  constexpr double Q = -0.5;
  constexpr double R = 0.6;
  const double expectedRollDot = P + Q * std::sin(RollRad) * std::tan(PitchRad)
                                 + R * std::cos(RollRad) * std::tan(PitchRad);
  const double expectedPitchDot = Q * std::cos(RollRad) - R * std::sin(RollRad);
  const double expectedHeadingDot =
      (Q * std::sin(RollRad) + R * std::cos(RollRad)) / std::cos(PitchRad);

  SetAttitudeAndBodyRates(aircraft, RollRad, PitchRad, P, Q, R);
  RequireEulerRates(aircraft,
      expectedRollDot,
      expectedPitchDot,
      expectedHeadingDot,
      "Coupled Euler motion");
}
} // namespace

int main() {
  try {
    TestExtractStateVector();
    TestApplyStateVector();
    TestExtractInputVector();
    TestExtractInputVectorWithoutThrottle();
    TestApplyInputVector();
    TestApplyInputVectorWithoutThrottle();
    TestStateVectorRoundTrip();
    TestInputVectorRoundTrip();

    sim::Aircraft aircraft;
    Require(
        aircraft.Initialize(sim::SimulationConfig{}, sim::InitialCondition{}),
        "Aircraft failed to initialize for derivative tests");
    TestStateDerivativeVectorOrdering(aircraft);
    TestEulerRateKinematics(aircraft);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }

  return 0;
}
