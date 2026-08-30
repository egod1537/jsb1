#pragma once

#include "sim/gnc/hold/PitchDynamics.hpp"
#include "sim/gnc/hold/YawDynamics.hpp"

#include <optional>

namespace gnc {
struct ControlContext {
  std::optional<PitchDynamics> pitchDynamics;
  std::optional<YawDynamics> yawDynamics;
};
} // namespace gnc
