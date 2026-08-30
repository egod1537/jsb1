#pragma once

#include "common/math/Math.hpp"
#include "sim/gnc/Controller.hpp"
#include "sim/gnc/hold/Px4RollHoldParameterMetadata.hpp"

#include <optional>

namespace sim {
class Aircraft;
struct Tick;
} // namespace sim

namespace gnc {
struct Px4RollHoldReferenceSettings {
  double timeConstantSec =
      GetPx4RollHoldParameterMetadata(Px4RollHoldParameter::TimeConstant)
          .defaultValue;
  double maximumRollRateRadPerSec = math::DegToRad(
      GetPx4RollHoldParameterMetadata(Px4RollHoldParameter::MaximumRollRate)
          .defaultValue);
  double rateProportionalGain = GetPx4RollHoldParameterMetadata(
      Px4RollHoldParameter::RateProportionalGain)
                                    .defaultValue;
  double rateIntegralGain =
      GetPx4RollHoldParameterMetadata(Px4RollHoldParameter::RateIntegralGain)
          .defaultValue;
  double rateDerivativeGain =
      GetPx4RollHoldParameterMetadata(Px4RollHoldParameter::RateDerivativeGain)
          .defaultValue;
  double rateFeedForwardGain =
      GetPx4RollHoldParameterMetadata(Px4RollHoldParameter::RateFeedForwardGain)
          .defaultValue;
  double integratorLimit =
      GetPx4RollHoldParameterMetadata(Px4RollHoldParameter::IntegratorLimit)
          .defaultValue;
  double trimAirspeedMps = 15.0;
  double stallAirspeedMps = 7.0;
  double trimRollCommand = 0.0;
};

struct Px4RollHoldReferenceDiagnostics {
  bool controlOutputValid = false;
  double targetRollRad = 0.0;
  double rollErrorRad = 0.0;
  double bodyRateSetpointRadPerSec = 0.0;
  double bodyRateErrorRadPerSec = 0.0;

  // Rate-controller contribution breakdown
  double rateProportionalTerm = 0.0;
  double rateIntegralTerm = 0.0;
  double rateDerivativeTerm = 0.0;
  double rateFeedForwardTerm = 0.0;

  // Integrator state and control-output pipeline
  double rateIntegrator = 0.0;
  double airspeedScaling = 1.0;
  double unscaledTorqueCommand = 0.0;
  double rawTorqueCommand = 0.0;
  double rollTorqueCommand = 0.0;
  double aileronCommand = 0.0;
  double trimRollCommand = 0.0;

  // Limiting state
  bool positiveSaturation = false;
  bool negativeSaturation = false;
  bool integratorLimited = false;
};

class Px4RollHoldReferenceController final : public Controller {
public:
  // Lifecycle
  void Reset() override;

  // Baseline Roll Hold execution
  bool IsEnabled() const;
  void SetEnabled(bool enabled);

  // PX4 parameter snapshot
  const Px4RollHoldReferenceSettings &GetSettings() const;
  void SetSettings(const Px4RollHoldReferenceSettings &settings);

  // Baseline output and diagnostics
  std::optional<double> OnTick(const sim::Aircraft &aircraft,
      const sim::Tick &tick, double targetRollRad);
  const Px4RollHoldReferenceDiagnostics &GetDiagnostics() const;

private:
  void UpdateIntegrator(double rateErrorRadPerSec, double dtSec,
      bool positiveSaturation, bool negativeSaturation);

  // Mode and PX4 parameter snapshot
  bool enabled_ = false;
  Px4RollHoldReferenceSettings settings_;

  // Controller state and last result
  double rateIntegrator_ = 0.0;
  Px4RollHoldReferenceDiagnostics diagnostics_;
};
} // namespace gnc
