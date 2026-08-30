#pragma once

#include "sim/control/ControlInput.hpp"
#include "sim/runtime/SimulationContracts.hpp"
#include "sim/gnc/TrimTypes.hpp"

namespace gui {
struct TrimRequested {
  gnc::TrimRequest request;
  bool fromCurrentState = false;
};

struct ManualControlChanged {
  control::ControlInput input;
};

struct PrimaryRollHoldConfigChanged {
  sim::PrimaryRollHoldConfig config;
};

struct BaselineRollHoldConfigChanged {
  sim::BaselineRollHoldConfig config;
};

enum class PrimaryRollHoldField {
  Enabled,
  TargetDeg,
  AngleProportionalGain,
  RateProportionalGain,
};

struct PrimaryRollHoldValueChanged {
  PrimaryRollHoldField field = PrimaryRollHoldField::Enabled;
  double value = 0.0;
};

enum class BaselineRollHoldField {
  Enabled,
  TargetDeg,
  TimeConstantSec,
  MaximumRateDegPerSec,
  RateProportionalGain,
  RateIntegralGain,
  RateDerivativeGain,
  RateFeedForwardGain,
  IntegratorLimit,
};

struct BaselineRollHoldValueChanged {
  BaselineRollHoldField field = BaselineRollHoldField::Enabled;
  double value = 0.0;
};

struct BaselineRollHoldTuningResetRequested {};

enum class TrimRequestField {
  Mode,
  AirspeedKts,
  AltitudeFt,
  FlightPathAngleDeg,
};

struct TrimRequestValueChanged {
  TrimRequestField field = TrimRequestField::Mode;
  double value = 0.0;
};

struct TrimExecutionRequested {
  bool fromCurrentState = false;
};

struct GNCViewStateChanged {
  bool primaryParametersOpen = true;
  bool baselineTuningOpen = false;
  bool baselineDiagnosticsOpen = true;
  bool trimResultOpen = true;
  bool trimResidualOpen = true;
};
} // namespace gui
