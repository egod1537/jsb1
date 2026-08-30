#pragma once

#include <string_view>

namespace telemetry::paths {
inline constexpr std::string_view AutopilotRollHoldCommandedRoll =
    "autopilot/roll_hold/commanded_roll";
inline constexpr std::string_view AutopilotRollHoldRoll =
    "autopilot/roll_hold/roll";
inline constexpr std::string_view AutopilotRollHoldRollError =
    "autopilot/roll_hold/roll_error";
inline constexpr std::string_view AutopilotRollHoldCommandedRollRate =
    "autopilot/roll_hold/commanded_roll_rate";
inline constexpr std::string_view AutopilotRollHoldRollRate =
    "autopilot/roll_hold/roll_rate";
inline constexpr std::string_view AutopilotRollHoldRollRateError =
    "autopilot/roll_hold/roll_rate_error";
inline constexpr std::string_view AutopilotRollHoldAileronCommand =
    "autopilot/roll_hold/aileron_command";

inline constexpr std::string_view AutopilotRollHoldRateProportionalTerm =
    "autopilot/roll_hold/rate_p_term";
inline constexpr std::string_view AutopilotRollHoldRateIntegralTerm =
    "autopilot/roll_hold/rate_i_term";
inline constexpr std::string_view AutopilotRollHoldRateDerivativeTerm =
    "autopilot/roll_hold/rate_d_term";
inline constexpr std::string_view AutopilotRollHoldRateFeedForwardTerm =
    "autopilot/roll_hold/rate_ff_term";

inline constexpr std::string_view AutopilotRollHoldUnscaledTorqueCommand =
    "autopilot/roll_hold/unscaled_torque_command";
inline constexpr std::string_view AutopilotRollHoldRawTorqueCommand =
    "autopilot/roll_hold/raw_torque_command";
inline constexpr std::string_view AutopilotRollHoldRollTorqueCommand =
    "autopilot/roll_hold/roll_torque_command";
inline constexpr std::string_view AutopilotRollHoldAirspeedScaling =
    "autopilot/roll_hold/airspeed_scaling";

inline constexpr std::string_view AutopilotRollHoldPositiveSaturation =
    "autopilot/roll_hold/positive_saturation";
inline constexpr std::string_view AutopilotRollHoldNegativeSaturation =
    "autopilot/roll_hold/negative_saturation";
inline constexpr std::string_view AutopilotRollHoldIntegratorLimited =
    "autopilot/roll_hold/integrator_limited";
inline constexpr std::string_view AutopilotRollHoldTrimRollCommand =
    "autopilot/roll_hold/trim_roll_command";

inline constexpr std::string_view AutopilotRollHoldRateIntegratorPositiveLimit =
    "autopilot/roll_hold/rate_integrator_positive_limit";
inline constexpr std::string_view AutopilotRollHoldRateIntegratorNegativeLimit =
    "autopilot/roll_hold/rate_integrator_negative_limit";
} // namespace telemetry::paths
