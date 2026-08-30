#include "sim/Simulation.hpp"
#include "sim/SimulationConfig.h"
#include "sim/StateLogger.hpp"
#include "sim/gnc/autopilot/IControllerInspectable.hpp"
#include "sim/gnc/autopilot/IAutopilot.hpp"
#include "sim/gnc/autopilot/IRollHoldAutopilot.hpp"
#include "sim/gnc/hold/Px4RollHoldReferenceController.hpp"
#include "sim/gnc/hold/RollHoldController.hpp"
#include "sim/linearization/LinearizationResult.hpp"
#include "sim/telemetry/AircraftTelemetry.hpp"
#include "sim/telemetry/AutopilotTelemetry.hpp"
#include "common/math/Math.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string_view>
#include <utility>

namespace {
constexpr double MetersPerSecondToKnots = 1.9438444924406048;

struct RollHoldTelemetrySnapshot {
  double commandedRollRad = 0.0;
  double rollRad = 0.0;
  double rollErrorRad = 0.0;
  std::optional<double> commandedRollRateRadPerSec;
  double rollRateRadPerSec = 0.0;
  std::optional<double> rollRateErrorRadPerSec;
};

gnc::TrimRequest TrimRequestFromInitialCondition(
    const sim::InitialCondition &initialCondition, gnc::TrimMode mode) {
  gnc::TrimRequest request{};
  request.mode = mode;
  request.airspeedKts = initialCondition.airspeedKts;
  request.altitudeFt = initialCondition.altitudeFt;
  request.flightPathAngleDeg = 0.0;
  return request;
}

} // namespace

namespace sim {
// public
Simulation::Simulation(std::unique_ptr<gnc::IAutopilot> autopilot) {
  AddComponent<control::FlightControlManager>(std::move(autopilot));
  AddComponent<StateLogger>();
}

Simulation::~Simulation() = default;

bool Simulation::Initialize(const SimulationConfig &config) {
  if (initialized_) {
    return true;
  }

  config_ = config;
  defaultInitialCondition_ = InitialCondition{};
  tickIndex_ = 0;
  telemetryRegistry_.Clear();
  errorTracker_.ClearError();

  auto *flightControlManager = GetComponent<control::FlightControlManager>();
  if (flightControlManager == nullptr) {
    errorTracker_.SetError("Flight control component is missing.");
    return false;
  }
  flightControlManager->ResetControllers();

  if (!aircraft_.Initialize(config_, defaultInitialCondition_)) {
    errorTracker_.SetError("Failed to initialize aircraft.");
    return false;
  }

  if (!ApplyInitialTrim(defaultInitialCondition_)) {
    return false;
  }

  if (!InitializeComponents()) {
    errorTracker_.SetErrorIfEmpty("Failed to initialize components.");
    ShutdownComponents();
    return false;
  }

  initialized_ = true;
  return true;
}

bool Simulation::Tick() { return Step(config_.GetDT()); }

bool Simulation::Step(double dtSec) {
  if (!initialized_) {
    errorTracker_.SetError("Simulation has not been initialized.");
    return false;
  }
  if (!std::isfinite(dtSec) || dtSec <= 0.0) {
    errorTracker_.SetError("Simulation step size must be finite and positive.");
    return false;
  }

  return ProcessStep(dtSec);
}

void Simulation::Shutdown() {
  if (initialized_) {
    ShutdownComponents();
  }

  initialized_ = false;
}

const SimulationConfig &Simulation::GetConfig() const { return config_; }

double Simulation::GetTickSizeSec() const { return config_.GetDT(); }

double Simulation::GetTime() const {
  return aircraft_.GetAircraftState().simulationTimeSec;
}

bool Simulation::Reset() { return Reset(defaultInitialCondition_); }

bool Simulation::Reset(const InitialCondition &initialCondition) {
  return Reset(initialCondition, SimulationResetOptions{});
}

bool Simulation::Reset(const InitialCondition &initialCondition,
    const SimulationResetOptions &options) {
  if (!initialized_) {
    errorTracker_.SetError("Simulation has not been initialized.");
    return false;
  }

  InitialCondition normalized = initialCondition;
  normalized.headingDeg = math::Wrap(normalized.headingDeg, 0.0, 360.0);

  std::string validationError;
  if (!ValidateInitialCondition(normalized, &validationError)) {
    errorTracker_.SetError(validationError);
    return false;
  }

  auto *flightControlManager = GetComponent<control::FlightControlManager>();
  if (flightControlManager == nullptr) {
    errorTracker_.SetError("Flight control component is missing.");
    return false;
  }

  if (!aircraft_.Reset(config_, normalized)) {
    errorTracker_.SetError(
        "Failed to reset aircraft with the requested initial condition.");
    return false;
  }
  if (options.environment.has_value()) {
    ApplyEnvironment(*options.environment);
  }

  flightControlManager->ResetControllers();

  if (options.runTrim) {
    if (!ApplyInitialTrim(normalized, options.trimMode)) {
      return false;
    }
  } else {
    trimService_.Clear();
    aircraft_.ResetSimulationTime();
  }
  if (options.environment.has_value()) {
    ApplyEnvironment(*options.environment);
  }

  tickIndex_ = 0;
  telemetryRegistry_.Clear();

  if (!ResetComponents()) {
    errorTracker_.SetErrorIfEmpty("Failed to reset components.");
    return false;
  }

  errorTracker_.ClearError();
  return true;
}

InitialCondition Simulation::GetCurrentCondition() const {
  return aircraft_.GetCurrentCondition();
}

const InitialCondition &Simulation::GetDefaultInitialCondition() const {
  return defaultInitialCondition_;
}

void Simulation::ApplyEnvironment(const FDMEnvironmentState &environment) {
  FDMState state{
      .flags = FDMStateFlags::Environment,
      .environment = environment,
  };
  aircraft_.ApplyFDMState(state);
}

gnc::TrimService &Simulation::GetTrimService() { return trimService_; }

const gnc::TrimService &Simulation::GetTrimService() const {
  return trimService_;
}

Aircraft &Simulation::GetAircraft() { return aircraft_; }

const Aircraft &Simulation::GetAircraft() const { return aircraft_; }

telemetry::TelemetryRegistry &Simulation::GetTelemetryRegistry() {
  return telemetryRegistry_;
}

const telemetry::TelemetryRegistry &Simulation::GetTelemetryRegistry() const {
  return telemetryRegistry_;
}

telemetry::TelemetryRegistry &Simulation::GetTelemetry() {
  return telemetryRegistry_;
}

const telemetry::TelemetryRegistry &Simulation::GetTelemetry() const {
  return telemetryRegistry_;
}

ErrorTracker &Simulation::GetErrorTracker() { return errorTracker_; }

const ErrorTracker &Simulation::GetErrorTracker() const {
  return errorTracker_;
}

bool Simulation::ProcessStep(double dtSec) {
  const sim::Tick tick = MakeTick(dtSec);

  if (!RunPreTickComponents(tick)) {
    errorTracker_.SetErrorIfEmpty("Simulation pre-tick component failed.");
    return false;
  }

  auto *flightControlManager = GetComponent<control::FlightControlManager>();
  if (flightControlManager == nullptr) {
    errorTracker_.SetError("Flight control component is missing.");
    return false;
  }

  if (!TickComponents(tick)) {
    errorTracker_.SetErrorIfEmpty("Simulation tick component failed.");
    return false;
  }

  if (!aircraft_.Step(dtSec)) {
    errorTracker_.SetError("JSBSim simulation stopped.");
    std::cerr << errorTracker_.GetLastError().value() << '\n';
    return false;
  }

  const sim::Tick postTick = MakeTick(dtSec);
  if (!RunPostTickComponents(postTick)) {
    errorTracker_.SetErrorIfEmpty("Simulation post-tick component failed.");
    return false;
  }

  PublishAutopilotTelemetry(postTick);
  PublishAircraftTelemetry(postTick);
  ++tickIndex_;

  return true;
}

sim::Tick Simulation::MakeTick(double dtSec) const {
  return sim::Tick{tickIndex_,
      dtSec,
      aircraft_.GetAircraftState().simulationTimeSec};
}

void Simulation::PublishAutopilotTelemetry(const sim::Tick &tick) {
  const auto publish = [this, &tick](std::string_view path, double value) {
    telemetryRegistry_.Publish(path, tick.simTimeSec, value);
  };

  const auto *flightControlManager =
      GetComponent<control::FlightControlManager>();
  if (flightControlManager == nullptr) {
    return;
  }

  const gnc::IAutopilot &autopilot = flightControlManager->GetAutopilot();
  const auto *rollHoldCapability =
      dynamic_cast<const gnc::IRollHoldAutopilot *>(&autopilot);
  const auto *controllers =
      dynamic_cast<const gnc::IControllerInspectable *>(&autopilot);
  double aileronCommand = 0.0;
  std::optional<RollHoldTelemetrySnapshot> snapshot;
  if (const auto *rollHold =
          controllers != nullptr
              ? controllers->GetController<gnc::RollHoldController>()
              : nullptr) {
    const gnc::RollHoldDiagnostics &diagnostics = rollHold->GetDiagnostics();
    aileronCommand = diagnostics.aileronCommand;
    if (diagnostics.controlOutputValid) {
      snapshot = RollHoldTelemetrySnapshot{
          .commandedRollRad = diagnostics.commandedRollRad,
          .rollRad = diagnostics.rollRad,
          .rollErrorRad = diagnostics.rollErrorRad,
          .commandedRollRateRadPerSec =
              diagnostics.commandedRollRateValid
                  ? std::optional<double>(
                        diagnostics.commandedRollRateRadPerSec)
                  : std::nullopt,
          .rollRateRadPerSec = diagnostics.rollRateRadPerSec,
          .rollRateErrorRadPerSec =
              diagnostics.commandedRollRateValid
                  ? std::optional<double>(diagnostics.rollRateErrorRadPerSec)
                  : std::nullopt,
      };
    }
  }
  if (const auto *px4RollReference =
          controllers != nullptr
              ? controllers
                    ->GetController<gnc::Px4RollHoldReferenceController>()
              : nullptr) {
    const gnc::Px4RollHoldReferenceDiagnostics &diagnostics =
        px4RollReference->GetDiagnostics();
    aileronCommand = diagnostics.aileronCommand;
    if (diagnostics.controlOutputValid) {
      const double integratorLimit =
          std::abs(px4RollReference->GetSettings().integratorLimit);
      snapshot = RollHoldTelemetrySnapshot{
          .commandedRollRad = diagnostics.targetRollRad,
          .rollRad = diagnostics.targetRollRad - diagnostics.rollErrorRad,
          .rollErrorRad = diagnostics.rollErrorRad,
          .commandedRollRateRadPerSec = diagnostics.bodyRateSetpointRadPerSec,
          .rollRateRadPerSec = diagnostics.bodyRateSetpointRadPerSec
                               - diagnostics.bodyRateErrorRadPerSec,
          .rollRateErrorRadPerSec = diagnostics.bodyRateErrorRadPerSec,
      };
      publish(telemetry::paths::AutopilotRollHoldRateProportionalTerm,
          diagnostics.rateProportionalTerm);
      publish(telemetry::paths::AutopilotRollHoldRateIntegralTerm,
          diagnostics.rateIntegralTerm);
      publish(telemetry::paths::AutopilotRollHoldRateDerivativeTerm,
          diagnostics.rateDerivativeTerm);
      publish(telemetry::paths::AutopilotRollHoldRateFeedForwardTerm,
          diagnostics.rateFeedForwardTerm);
      publish(telemetry::paths::AutopilotRollHoldUnscaledTorqueCommand,
          diagnostics.unscaledTorqueCommand);
      publish(telemetry::paths::AutopilotRollHoldRawTorqueCommand,
          diagnostics.rawTorqueCommand);
      publish(telemetry::paths::AutopilotRollHoldRollTorqueCommand,
          diagnostics.rollTorqueCommand);
      publish(telemetry::paths::AutopilotRollHoldAirspeedScaling,
          diagnostics.airspeedScaling);
      publish(telemetry::paths::AutopilotRollHoldPositiveSaturation,
          diagnostics.positiveSaturation ? 1.0 : 0.0);
      publish(telemetry::paths::AutopilotRollHoldNegativeSaturation,
          diagnostics.negativeSaturation ? 1.0 : 0.0);
      publish(telemetry::paths::AutopilotRollHoldIntegratorLimited,
          diagnostics.integratorLimited ? 1.0 : 0.0);
      publish(telemetry::paths::AutopilotRollHoldTrimRollCommand,
          diagnostics.trimRollCommand);
      publish(telemetry::paths::AutopilotRollHoldRateIntegratorPositiveLimit,
          integratorLimit);
      publish(telemetry::paths::AutopilotRollHoldRateIntegratorNegativeLimit,
          -integratorLimit);
    }
  }
  const double commandedRollRad =
      snapshot ? snapshot->commandedRollRad
               : (rollHoldCapability != nullptr
                         ? rollHoldCapability->GetTargetRollRad()
                         : 0.0);
  publish(telemetry::paths::AutopilotRollHoldCommandedRoll,
      math::RadToDeg(commandedRollRad));

  if (!snapshot) {
    return;
  }

  publish(telemetry::paths::AutopilotRollHoldAileronCommand, aileronCommand);
  publish(telemetry::paths::AutopilotRollHoldRoll,
      math::RadToDeg(snapshot->rollRad));
  publish(telemetry::paths::AutopilotRollHoldRollError,
      math::RadToDeg(snapshot->rollErrorRad));
  publish(telemetry::paths::AutopilotRollHoldRollRate,
      math::RadToDeg(snapshot->rollRateRadPerSec));
  if (snapshot->commandedRollRateRadPerSec) {
    publish(telemetry::paths::AutopilotRollHoldCommandedRollRate,
        math::RadToDeg(*snapshot->commandedRollRateRadPerSec));
  }
  if (snapshot->rollRateErrorRadPerSec) {
    publish(telemetry::paths::AutopilotRollHoldRollRateError,
        math::RadToDeg(*snapshot->rollRateErrorRadPerSec));
  }
}

void Simulation::PublishAircraftTelemetry(const sim::Tick &tick) {
  const AircraftState state = aircraft_.GetAircraftState();
  const AircraftStateDerivative derivative =
      aircraft_.GetAircraftStateDerivative();
  const auto publish = [this, &tick](std::string_view path, double value) {
    telemetryRegistry_.Publish(path, tick.simTimeSec, value);
  };

  publish(telemetry::paths::AircraftAeroAlpha, state.alphaDeg);
  publish(telemetry::paths::AircraftAeroBeta, state.betaDeg);
  publish(telemetry::paths::AircraftAttitudeRoll, state.rollDeg);
  publish(telemetry::paths::AircraftAttitudePitch, state.pitchDeg);
  publish(telemetry::paths::AircraftAttitudeHeading, state.headingDeg);
  publish(telemetry::paths::AircraftNavigationCourse, state.courseDeg);
  publish(telemetry::paths::AircraftBodyVelocityU, state.uMps);
  publish(telemetry::paths::AircraftBodyVelocityV, state.vMps);
  publish(telemetry::paths::AircraftBodyVelocityW, state.wMps);
  publish(telemetry::paths::AircraftRateP, state.pDegPerSec);
  publish(telemetry::paths::AircraftRateQ, state.qDegPerSec);
  publish(telemetry::paths::AircraftRateR, state.rDegPerSec);
  publish(telemetry::paths::AircraftCalibratedAirspeed,
      state.calibratedAirspeedKts);
  publish(telemetry::paths::AircraftTrueAirspeed,
      state.trueAirspeedMps * MetersPerSecondToKnots);
  publish(telemetry::paths::AircraftAltitudeAgl, state.altitudeAglFt);
  publish(telemetry::paths::AircraftBodyAccelerationU, derivative.uDotMps2);
  publish(telemetry::paths::AircraftBodyAccelerationV, derivative.vDotMps2);
  publish(telemetry::paths::AircraftBodyAccelerationW, derivative.wDotMps2);
  publish(telemetry::paths::AircraftAngularAccelerationP,
      derivative.pDotDegPerSec2);
  publish(telemetry::paths::AircraftAngularAccelerationQ,
      derivative.qDotDegPerSec2);
  publish(telemetry::paths::AircraftAngularAccelerationR,
      derivative.rDotDegPerSec2);
  publish(telemetry::paths::AircraftControlAileron,
      aircraft_.GetControls().GetInput().aileron);
  publish(telemetry::paths::AircraftControlRudder,
      aircraft_.GetControls().GetInput().rudder);
}

bool Simulation::ApplyInitialTrim(const InitialCondition &initialCondition,
    gnc::TrimMode mode) {
  auto *flightControlManager = GetComponent<control::FlightControlManager>();
  if (flightControlManager == nullptr) {
    errorTracker_.SetError("Flight control component is missing.");
    return false;
  }

  if (!trimService_.Compute(aircraft_,
          TrimRequestFromInitialCondition(initialCondition, mode))) {
    errorTracker_.SetError("Initial trim failed.");
    std::cerr << "Initial trim failed: " << errorTracker_.GetLastError().value()
              << '\n';
    return false;
  }

  if (!trimService_.ApplyStored(aircraft_)) {
    errorTracker_.SetError("Failed to apply stored initial trim.");
    std::cerr << errorTracker_.GetLastError().value() << '\n';
    return false;
  }

  if (const gnc::TrimResult *trimResult = trimService_.GetResult()) {
    flightControlManager->SynchronizeWithTrimResult(aircraft_, *trimResult);
  }

  aircraft_.ResetSimulationTime();
  return true;
}

bool Simulation::InitializeComponent(Component &component) {
  if (component.initialized_) {
    return true;
  }

  if (!component.OnInitialize()) {
    component.OnShutdown();
    return false;
  }

  component.initialized_ = true;
  return true;
}

bool Simulation::InitializeComponents() {
  for (std::size_t index = 0; index < components_.size(); ++index) {
    if (!InitializeComponent(*components_[index])) {
      return false;
    }
  }

  return true;
}

bool Simulation::ResetComponents() {
  for (const auto &component : components_) {
    if (component->initialized_ && !component->OnReset()) {
      return false;
    }
  }

  return true;
}

bool Simulation::RunPreTickComponents(const sim::Tick &tick) {
  for (const auto &component : components_) {
    if (component->initialized_ && !component->OnPreTick(tick)) {
      return false;
    }
  }

  return true;
}

bool Simulation::TickComponents(const sim::Tick &tick) {
  for (const auto &component : components_) {
    if (component->initialized_ && !component->OnTick(tick)) {
      return false;
    }
  }

  return true;
}

bool Simulation::RunPostTickComponents(const sim::Tick &tick) {
  for (const auto &component : components_) {
    if (component->initialized_ && !component->OnPostTick(tick)) {
      return false;
    }
  }

  return true;
}

void Simulation::ShutdownComponents() {
  for (auto iterator = components_.rbegin(); iterator != components_.rend();
      ++iterator) {
    if ((*iterator)->initialized_) {
      (*iterator)->OnShutdown();
      (*iterator)->initialized_ = false;
    }
  }
}

Component *Simulation::FindComponent(const std::type_info &type) {
  for (const auto &component : components_) {
    if (typeid(*component) == type) {
      return component.get();
    }
  }

  return nullptr;
}

const Component *Simulation::FindComponent(const std::type_info &type) const {
  for (const auto &component : components_) {
    if (typeid(*component) == type) {
      return component.get();
    }
  }

  return nullptr;
}

} // namespace sim
