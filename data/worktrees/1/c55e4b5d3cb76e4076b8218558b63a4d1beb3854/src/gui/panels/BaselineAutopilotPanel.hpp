#pragma once

#include "gui/architecture/EventSink.hpp"
#include "gui/features/gnc/GNCEvents.hpp"
#include "sim/gnc/hold/Px4RollHoldParameterMetadata.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace gui {
struct BaselineAutopilotPanelState {
  BaselineAutopilotPanelState();

  // Baseline Roll Hold
  bool rollHold = false;
  double rollTargetDeg = 0.0;

  // PX4 v1.17 Roll Hold tuning
  double px4RollTimeConstantSec = 0.0;
  double px4RollMaximumRateDegPerSec = 0.0;
  double px4RollRateProportionalGain = 0.0;
  double px4RollRateIntegralGain = 0.0;
  double px4RollRateDerivativeGain = 0.0;
  double px4RollRateFeedForwardGain = 0.0;
  double px4RollIntegratorLimit = 0.0;

  // Foldout state
  bool px4RollTuningOpen = false;
  bool px4RollDiagnosticsOpen = true;
};

struct BaselinePx4RollHoldParameterBinding {
  BaselineRollHoldField field;
  gnc::Px4RollHoldParameter parameter;
  double BaselineAutopilotPanelState::*value;
};

inline constexpr std::array<BaselinePx4RollHoldParameterBinding, 7>
    BaselinePx4RollHoldParameterBindings{{
        {BaselineRollHoldField::TimeConstantSec,
            gnc::Px4RollHoldParameter::TimeConstant,
            &BaselineAutopilotPanelState::px4RollTimeConstantSec},
        {BaselineRollHoldField::MaximumRateDegPerSec,
            gnc::Px4RollHoldParameter::MaximumRollRate,
            &BaselineAutopilotPanelState::px4RollMaximumRateDegPerSec},
        {BaselineRollHoldField::RateProportionalGain,
            gnc::Px4RollHoldParameter::RateProportionalGain,
            &BaselineAutopilotPanelState::px4RollRateProportionalGain},
        {BaselineRollHoldField::RateIntegralGain,
            gnc::Px4RollHoldParameter::RateIntegralGain,
            &BaselineAutopilotPanelState::px4RollRateIntegralGain},
        {BaselineRollHoldField::RateDerivativeGain,
            gnc::Px4RollHoldParameter::RateDerivativeGain,
            &BaselineAutopilotPanelState::px4RollRateDerivativeGain},
        {BaselineRollHoldField::RateFeedForwardGain,
            gnc::Px4RollHoldParameter::RateFeedForwardGain,
            &BaselineAutopilotPanelState::px4RollRateFeedForwardGain},
        {BaselineRollHoldField::IntegratorLimit,
            gnc::Px4RollHoldParameter::IntegratorLimit,
            &BaselineAutopilotPanelState::px4RollIntegratorLimit},
    }};

inline const BaselinePx4RollHoldParameterBinding *
FindBaselinePx4RollHoldParameterBinding(BaselineRollHoldField field) {
  for (const BaselinePx4RollHoldParameterBinding &binding :
      BaselinePx4RollHoldParameterBindings) {
    if (binding.field == field) {
      return &binding;
    }
  }
  return nullptr;
}

inline void ResetBaselinePx4RollHoldTuning(BaselineAutopilotPanelState &state) {
  for (const BaselinePx4RollHoldParameterBinding &binding :
      BaselinePx4RollHoldParameterBindings) {
    const auto &metadata =
        gnc::GetPx4RollHoldParameterMetadata(binding.parameter);
    state.*(binding.value) = metadata.defaultValue;
  }
}

inline bool SetBaselinePx4RollHoldParameter(BaselineAutopilotPanelState &state,
    BaselineRollHoldField field, double value) {
  const BaselinePx4RollHoldParameterBinding *binding =
      FindBaselinePx4RollHoldParameterBinding(field);
  if (binding == nullptr || !std::isfinite(value)) {
    return false;
  }

  const auto &metadata =
      gnc::GetPx4RollHoldParameterMetadata(binding->parameter);
  const double clamped = std::clamp(value, metadata.minimum, metadata.maximum);
  constexpr double PrecisionScale = 1000.0;
  state.*(binding->value) =
      std::clamp(std::round(clamped * PrecisionScale) / PrecisionScale,
          metadata.minimum,
          metadata.maximum);
  return true;
}

inline BaselineAutopilotPanelState::BaselineAutopilotPanelState() {
  ResetBaselinePx4RollHoldTuning(*this);
}

struct BaselineAutopilotPanelProps {
  BaselineAutopilotPanelState &state;
  double currentRollDeg = 0.0;
  double currentRollRateDegPerSec = 0.0;
  double currentAileron = 0.0;
  bool rollHoldActive = false;
  architecture::EventSink<BaselineRollHoldValueChanged> valueEvents;
  architecture::EventSink<BaselineRollHoldTuningResetRequested> resetEvents;
  double px4RollAileronCommand = 0.0;
  double px4RollRateSetpointDegPerSec = 0.0;
  double px4RollErrorDeg = 0.0;
  double px4AirspeedScaling = 1.0;
};

class BaselineAutopilotPanel {
public:
  static void Draw(const BaselineAutopilotPanelProps &props);
};
} // namespace gui
