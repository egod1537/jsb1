#include "sim/gnc/hold/RollHoldController.hpp"

#include "sim/Aircraft.hpp"

namespace gnc {
void RollHoldController::Reset() { ClearDiagnostics(); }

bool RollHoldController::IsEnabled() const { return enabled_; }

void RollHoldController::SetEnabled(bool enabled) {
  if (enabled_ == enabled) {
    return;
  }

  enabled_ = enabled;
  if (!enabled_) {
    ClearDiagnostics();
  }
}

const RollHoldSettings &RollHoldController::GetSettings() const {
  return settings_;
}

void RollHoldController::SetSettings(const RollHoldSettings &settings) {
  settings_ = settings;
}

double RollHoldController::GetTrimAileron() const { return trimAileron_; }

void RollHoldController::SetTrimAileron(double trimAileron) {
  trimAileron_ = trimAileron;
}

const RollHoldDiagnostics &RollHoldController::GetDiagnostics() const {
  return diagnostics_;
}

std::optional<double> RollHoldController::OnTick(const sim::Aircraft &aircraft,
    const sim::Tick &, const ControlContext &) {
  if (!enabled_) {
    ClearDiagnostics();
    return std::nullopt;
  }

  return ComputeControlOutput(aircraft, settings_.targetRollRad);
}

std::optional<double> RollHoldController::OnTick(const sim::Aircraft &aircraft,
    const sim::Tick &, const ControlContext &, double commandedRollRad) {
  return ComputeControlOutput(aircraft, commandedRollRad);
}

RollHoldController::RollAttitudeLoopOutput
RollHoldController::ComputeRollAttitudeLoop(const sim::Aircraft &aircraft,
    double commandedRollRad) const {
  const auto &prop = aircraft.GetProperties();
  const double rollRad = prop.Roll().Rad();

  return {
      .commandedRollRad = commandedRollRad,
      .rollRad = rollRad,
      .rollErrorRad = commandedRollRad - rollRad,
      .commandedRollRateRadPerSec =
          ComputeRollRateSetpoint(commandedRollRad - rollRad),
  };
}

std::optional<double> RollHoldController::ComputeRollRateSetpoint(double) const {
  // Intentionally unimplemented. Use settings_.attitudeLoop.proportionalGain
  // when the outer P loop is added.
  return std::nullopt;
}

std::optional<double> RollHoldController::ComputeAileronCommand(
    double, double) const {
  // Intentionally unimplemented. Use settings_.rateLoop.proportionalGain when
  // the inner P loop is added.
  return std::nullopt;
}

std::optional<double> RollHoldController::ComputeControlOutput(
    const sim::Aircraft &aircraft, double commandedRollRad) {
  const RollAttitudeLoopOutput attitudeOutput =
      ComputeRollAttitudeLoop(aircraft, commandedRollRad);
  std::optional<double> aileronCommand;
  if (attitudeOutput.commandedRollRateRadPerSec) {
    aileronCommand = ComputeAileronCommand(
        *attitudeOutput.commandedRollRateRadPerSec,
        aircraft.GetProperties().P().RadPerSec());
  }

  StoreDiagnostics(aircraft, attitudeOutput, aileronCommand);
  return aileronCommand;
}

void RollHoldController::StoreDiagnostics(const sim::Aircraft &aircraft,
    const RollAttitudeLoopOutput &attitudeOutput,
    std::optional<double> aileronCommand) {
  const double rollRateRadPerSec = aircraft.GetProperties().P().RadPerSec();
  const bool commandedRollRateValid =
      attitudeOutput.commandedRollRateRadPerSec.has_value();
  const double commandedRollRateRadPerSec =
      attitudeOutput.commandedRollRateRadPerSec.value_or(0.0);

  diagnostics_ = {
      .controlOutputValid = aileronCommand.has_value(),
      .commandedRollRad = attitudeOutput.commandedRollRad,
      .rollRad = attitudeOutput.rollRad,
      .rollErrorRad = attitudeOutput.rollErrorRad,
      .commandedRollRateValid = commandedRollRateValid,
      .commandedRollRateRadPerSec = commandedRollRateRadPerSec,
      .rollRateRadPerSec = rollRateRadPerSec,
      .rollRateErrorRadPerSec =
          commandedRollRateValid
              ? commandedRollRateRadPerSec - rollRateRadPerSec
              : 0.0,
      .aileronCommand = aileronCommand.value_or(0.0),
  };
}

void RollHoldController::ClearDiagnostics() { diagnostics_ = {}; }

} // namespace gnc
