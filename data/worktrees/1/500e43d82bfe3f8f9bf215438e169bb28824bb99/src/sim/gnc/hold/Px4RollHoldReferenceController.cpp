#include "sim/gnc/hold/Px4RollHoldReferenceController.hpp"

#include "sim/Aircraft.hpp"
#include "sim/Tick.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr double MinimumPx4DtSec = 0.002;
constexpr double MaximumPx4DtSec = 0.040;
constexpr double IntegralReductionRateRadPerSec =
    6.981317007977318; // 400 deg/s
} // namespace

namespace gnc {
void Px4RollHoldReferenceController::Reset() {
  rateIntegrator_ = 0.0;
  diagnostics_ = {};
}

bool Px4RollHoldReferenceController::IsEnabled() const { return enabled_; }

void Px4RollHoldReferenceController::SetEnabled(bool enabled) {
  if (enabled_ == enabled) {
    return;
  }

  enabled_ = enabled;
  Reset();
}

const Px4RollHoldReferenceSettings &
Px4RollHoldReferenceController::GetSettings() const {
  return settings_;
}

void Px4RollHoldReferenceController::SetSettings(
    const Px4RollHoldReferenceSettings &settings) {
  settings_ = settings;
}

std::optional<double> Px4RollHoldReferenceController::OnTick(
    const sim::Aircraft &aircraft, const sim::Tick &tick,
    double targetRollRad) {
  if (!enabled_) {
    return std::nullopt;
  }

  const auto &properties = aircraft.GetProperties();
  const double rollErrorRad = targetRollRad - properties.Roll().Rad();
  const double timeConstantSec = std::max(settings_.timeConstantSec, 1.0e-6);
  const double maximumRollRate =
      std::max(settings_.maximumRollRateRadPerSec, 0.0);
  const double rateSetpoint = std::clamp(rollErrorRad / timeConstantSec,
      -maximumRollRate,
      maximumRollRate);
  const double rateError = rateSetpoint - properties.P().RadPerSec();

  const double calibratedAirspeedMps = properties.CalibratedAirspeed().Mps();
  const double minimumAirspeed = std::max(settings_.stallAirspeedMps, 0.1);
  const double constrainedAirspeed =
      std::max(calibratedAirspeedMps, minimumAirspeed);
  const double airspeedScaling =
      settings_.trimAirspeedMps / constrainedAirspeed;
  const double scaledFeedForward =
      settings_.rateFeedForwardGain / std::max(airspeedScaling, 1.0e-6);

  const double rateProportionalTerm =
      settings_.rateProportionalGain * rateError;
  const double rateIntegralTerm = rateIntegrator_;
  const double rateDerivativeTerm =
      -settings_.rateDerivativeGain * properties.P().DotRadPerSec2();
  const double rateFeedForwardTerm = scaledFeedForward * rateSetpoint;
  const double unscaledTorque = rateProportionalTerm + rateIntegralTerm
                                + rateDerivativeTerm + rateFeedForwardTerm;
  const double scalingSquared = airspeedScaling * airspeedScaling;
  const double rawTorqueCommand = unscaledTorque * scalingSquared
                                  + settings_.trimRollCommand * scalingSquared;
  const double rollTorqueCommand = std::clamp(rawTorqueCommand, -1.0, 1.0);
  const bool positiveSaturation = rawTorqueCommand > 1.0;
  const bool negativeSaturation = rawTorqueCommand < -1.0;

  // PX4 positive roll torque and the C172x normalized aileron input both
  // produce positive roll. The Rascal bridge's channel reversal belongs to
  // its servo allocation and must not be applied to this direct C172x input.
  const double aileronCommand = rollTorqueCommand;

  const double dtSec = std::clamp(tick.dtSec, MinimumPx4DtSec, MaximumPx4DtSec);
  UpdateIntegrator(rateError, dtSec, positiveSaturation, negativeSaturation);
  const double integratorLimit = std::max(settings_.integratorLimit, 0.0);
  const bool integratorLimited =
      integratorLimit > 0.0 && std::abs(rateIntegrator_) >= integratorLimit;

  diagnostics_ = {
      .controlOutputValid = true,
      .targetRollRad = targetRollRad,
      .rollErrorRad = rollErrorRad,
      .bodyRateSetpointRadPerSec = rateSetpoint,
      .bodyRateErrorRadPerSec = rateError,
      .rateProportionalTerm = rateProportionalTerm,
      .rateIntegralTerm = rateIntegralTerm,
      .rateDerivativeTerm = rateDerivativeTerm,
      .rateFeedForwardTerm = rateFeedForwardTerm,
      .rateIntegrator = rateIntegrator_,
      .airspeedScaling = airspeedScaling,
      .unscaledTorqueCommand = unscaledTorque,
      .rawTorqueCommand = rawTorqueCommand,
      .rollTorqueCommand = rollTorqueCommand,
      .aileronCommand = aileronCommand,
      .trimRollCommand = settings_.trimRollCommand,
      .positiveSaturation = positiveSaturation,
      .negativeSaturation = negativeSaturation,
      .integratorLimited = integratorLimited,
  };
  return aileronCommand;
}

const Px4RollHoldReferenceDiagnostics &
Px4RollHoldReferenceController::GetDiagnostics() const {
  return diagnostics_;
}

void Px4RollHoldReferenceController::UpdateIntegrator(double rateErrorRadPerSec,
    double dtSec, bool positiveSaturation, bool negativeSaturation) {
  if (positiveSaturation) {
    rateErrorRadPerSec = std::min(rateErrorRadPerSec, 0.0);
  }
  if (negativeSaturation) {
    rateErrorRadPerSec = std::max(rateErrorRadPerSec, 0.0);
  }

  const double normalizedError =
      rateErrorRadPerSec / IntegralReductionRateRadPerSec;
  const double integralFactor =
      std::max(0.0, 1.0 - normalizedError * normalizedError);
  const double nextIntegrator = rateIntegrator_
                                + integralFactor * settings_.rateIntegralGain
                                      * rateErrorRadPerSec * dtSec;
  if (std::isfinite(nextIntegrator)) {
    const double limit = std::max(settings_.integratorLimit, 0.0);
    rateIntegrator_ = std::clamp(nextIntegrator, -limit, limit);
  }
}
} // namespace gnc
