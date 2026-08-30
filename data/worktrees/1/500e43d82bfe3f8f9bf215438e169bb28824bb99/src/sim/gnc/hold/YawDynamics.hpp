#pragma once

namespace gnc {
struct YawDynamics {
  double aBetaBeta{};
  double aBetaR{};
  double aRBeta{};
  double aRR{};

  double bBetaRudder{};
  double bRRudder{};
};
} // namespace gnc
