#pragma once

namespace gnc {
struct RollAttitudeLoopSettings {
  double proportionalGain = 0.0;
};

struct RollRateLoopSettings {
  double proportionalGain = 0.0;
};

struct RollHoldSettings {
  double targetRollRad{};
  RollAttitudeLoopSettings attitudeLoop;
  RollRateLoopSettings rateLoop;
};
} // namespace gnc
