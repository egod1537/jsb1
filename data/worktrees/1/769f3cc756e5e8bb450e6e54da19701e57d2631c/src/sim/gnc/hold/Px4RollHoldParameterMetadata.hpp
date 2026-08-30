#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace gnc {
enum class Px4RollHoldParameter {
  TimeConstant,
  MaximumRollRate,
  RateProportionalGain,
  RateIntegralGain,
  RateDerivativeGain,
  RateFeedForwardGain,
  IntegratorLimit,
};

struct Px4RollHoldParameterMetadata {
  Px4RollHoldParameter parameter;
  std::string_view name;
  std::string_view description;
  std::string_view unit;
  double minimum;
  double maximum;
  double defaultValue;
  double increment;
};

// PX4 v1.17 fixed-wing attitude and rate controller parameter metadata.
inline constexpr std::array<Px4RollHoldParameterMetadata, 7>
    Px4RollHoldParameters{{
        {Px4RollHoldParameter::TimeConstant,
            "FW_R_TC",
            "Roll time constant",
            "s",
            0.2,
            1.0,
            0.4,
            0.05},
        {Px4RollHoldParameter::MaximumRollRate,
            "FW_R_RMAX",
            "Maximum roll rate",
            "deg/s",
            0.0,
            180.0,
            70.0,
            0.5},
        {Px4RollHoldParameter::RateProportionalGain,
            "FW_RR_P",
            "Roll rate proportional gain",
            "%/rad/s",
            0.0,
            10.0,
            0.05,
            0.005},
        {Px4RollHoldParameter::RateIntegralGain,
            "FW_RR_I",
            "Roll rate integrator gain",
            "%/rad",
            0.0,
            10.0,
            0.1,
            0.01},
        {Px4RollHoldParameter::RateDerivativeGain,
            "FW_RR_D",
            "Roll rate derivative gain",
            "%/rad/s",
            0.0,
            10.0,
            0.0,
            0.005},
        {Px4RollHoldParameter::RateFeedForwardGain,
            "FW_RR_FF",
            "Roll rate feed forward",
            "%/rad/s",
            0.0,
            10.0,
            0.5,
            0.05},
        {Px4RollHoldParameter::IntegratorLimit,
            "FW_RR_IMAX",
            "Roll integrator limit",
            "",
            0.0,
            1.0,
            0.2,
            0.05},
    }};

constexpr const Px4RollHoldParameterMetadata &GetPx4RollHoldParameterMetadata(
    Px4RollHoldParameter parameter) {
  return Px4RollHoldParameters[static_cast<std::size_t>(parameter)];
}
} // namespace gnc
