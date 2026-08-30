#pragma once

namespace gui {
inline constexpr double RollSettlingToleranceDeg = 0.5;
inline constexpr double RollLimitToleranceDeg = 1.0;

struct RollTrackingAcceptance {
  double settlingUpperDeg = 0.0;
  double settlingLowerDeg = 0.0;
  double overshootLimitDeg = 0.0;
  double undershootLimitDeg = 0.0;
};

constexpr RollTrackingAcceptance MakeRollTrackingAcceptance(
    double commandedRollDeg) {
  return {
      .settlingUpperDeg = commandedRollDeg + RollSettlingToleranceDeg,
      .settlingLowerDeg = commandedRollDeg - RollSettlingToleranceDeg,
      .overshootLimitDeg = commandedRollDeg + RollLimitToleranceDeg,
      .undershootLimitDeg = commandedRollDeg - RollLimitToleranceDeg,
  };
}
} // namespace gui
