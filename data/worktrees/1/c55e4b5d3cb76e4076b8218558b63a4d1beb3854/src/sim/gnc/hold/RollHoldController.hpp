#pragma once

#include "sim/gnc/Controller.hpp"
#include "sim/gnc/hold/RollHoldSettings.hpp"

#include <optional>

namespace sim {
class Aircraft;
struct Tick;
} // namespace sim

namespace gnc {
struct ControlContext;

struct RollHoldDiagnostics {
  bool controlOutputValid = false;
  double commandedRollRad = 0.0;

  double rollRad = 0.0;
  double rollErrorRad = 0.0;

  bool commandedRollRateValid = false;
  double commandedRollRateRadPerSec = 0.0;
  double rollRateRadPerSec = 0.0;
  double rollRateErrorRadPerSec = 0.0;

  double aileronCommand = 0.0;
};

class RollHoldController final : public Controller {
public:
  // Lifecycle
  void Reset() override;

  // Mode
  bool IsEnabled() const;
  void SetEnabled(bool enabled);

  // Configuration
  const RollHoldSettings &GetSettings() const;
  void SetSettings(const RollHoldSettings &settings);

  // Trim reference
  double GetTrimAileron() const;
  void SetTrimAileron(double trimAileron);

  // Diagnostics
  const RollHoldDiagnostics &GetDiagnostics() const;

  // Standalone control output
  std::optional<double> OnTick(const sim::Aircraft &aircraft,
      const sim::Tick &tick, const ControlContext &context);

  // Cascaded control output
  std::optional<double> OnTick(const sim::Aircraft &aircraft,
      const sim::Tick &tick, const ControlContext &context,
      double commandedRollRad);

private:
  struct RollAttitudeLoopOutput {
    double commandedRollRad = 0.0;
    double rollRad = 0.0;
    double rollErrorRad = 0.0;
    std::optional<double> commandedRollRateRadPerSec;
  };

  // Roll-loop stages
  RollAttitudeLoopOutput ComputeRollAttitudeLoop(const sim::Aircraft &aircraft,
      double commandedRollRad) const;
  std::optional<double> ComputeRollRateSetpoint(double rollErrorRad) const;
  std::optional<double> ComputeAileronCommand(
      double commandedRollRateRadPerSec,
      double rollRateRadPerSec) const;
  std::optional<double> ComputeControlOutput(const sim::Aircraft &aircraft,
      double commandedRollRad);

  // Diagnostics
  void StoreDiagnostics(const sim::Aircraft &aircraft,
      const RollAttitudeLoopOutput &attitudeOutput,
      std::optional<double> aileronCommand);
  void ClearDiagnostics();

  // Mode and configuration
  bool enabled_ = false;
  RollHoldSettings settings_;

  // Trim reference
  double trimAileron_ = 0.0;

  // Last control result
  RollHoldDiagnostics diagnostics_;
};
} // namespace gnc
