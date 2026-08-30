#include "sim/gnc/hold/YawDamperController.hpp"

#include "sim/Aircraft.hpp"
#include "sim/Tick.hpp"
#include "sim/gnc/ControlContext.hpp"
#include "sim/gnc/hold/YawDynamics.hpp"
#include <cmath>
#include <optional>

namespace gnc {
void YawDamperController::Reset() {
  washoutFilter_.Reset();
  diagnostics_ = {};
}

bool YawDamperController::IsEnabled() const { return enabled_; }

void YawDamperController::SetEnabled(bool enabled) {
  if (enabled_ == enabled) {
    return;
  }

  enabled_ = enabled;
  if (!enabled_) {
    diagnostics_ = {};
  }
}

double YawDamperController::GetTrimRudder() const { return trimRudder_; }

void YawDamperController::SetTrimRudder(double trimRudder) {
  trimRudder_ = trimRudder;
}

const YawDamperDiagnostics &YawDamperController::GetDiagnostics() const {
  return diagnostics_;
}

std::optional<double> GetKR(const YawDynamics &dynamics) {
  const double a = dynamics.aBetaBeta;
  const double b = dynamics.aBetaR;
  const double c = dynamics.aRBeta;
  const double d = dynamics.aRR;
  const double e = dynamics.bBetaRudder;
  const double f = dynamics.bRRudder;

  if (!std::isfinite(f) || std::abs(f) < 1e-9) {
    return std::nullopt;
  }

  const double gainBase = (d * f + e * c) / (f * f);

  const double discriminant =
      gainBase * gainBase - (a * a + d * d + 2.0 * b * c) / (f * f);
  if (!std::isfinite(discriminant) || discriminant < 0.0) {
    return std::nullopt;
  }

  const double kR = -gainBase + std::sqrt(discriminant);
  if (!std::isfinite(kR) || kR <= 0.0) {
    return std::nullopt;
  }
  return kR;
}

std::optional<double> YawDamperController::OnTick(const sim::Aircraft &aircraft,
    const sim::Tick &tick, const ControlContext &ctx) {
  if (!enabled_) {
    return std::nullopt;
  }

  diagnostics_ = {};

  const auto &prop = aircraft.GetProperties();
  const double r = prop.R().RadPerSec();

  const auto &dynamics = ctx.yawDynamics;
  if (!dynamics) {
    return std::nullopt;
  }

  const double determinant =
      dynamics->aBetaBeta * dynamics->aRR - dynamics->aBetaR * dynamics->aRBeta;
  if (!std::isfinite(determinant) || determinant <= 0.0) {
    return std::nullopt;
  }
  const double wN = std::sqrt(determinant);

  const double pWo = wN / 10.0;

  const auto kR = GetKR(*dynamics);
  if (!kR) {
    return std::nullopt;
  }

  const double filteredR = washoutFilter_.Update(r, pWo, tick.dtSec);
  const double newRudder = GetTrimRudder() + *kR * filteredR;
  diagnostics_ = {
      .filteredRRadPerSec = filteredR,
      .rudderCommand = newRudder,
  };
  return newRudder;
}
} // namespace gnc
