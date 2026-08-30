#pragma once

namespace opts {

namespace debug {
inline constexpr bool PrintAircraftState = false;
inline constexpr bool PrintControllerOutput = false;
} // namespace debug

namespace experiment {
inline constexpr bool ValidateLinearModel = false;
inline constexpr double ValidationIntervalSec = 0.2;
} // namespace experiment

} // namespace opts
