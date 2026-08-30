#include "sim/gnc/hold/PitchHoldController.hpp"
#include "sim/Aircraft.hpp"
#include "sim/gnc/ControlContext.hpp"
#include "sim/jsbsim/Properties.hpp"

namespace gnc {
void PitchHoldController::Reset() {}

bool PitchHoldController::IsEnabled() const { return enabled_; }

void PitchHoldController::SetEnabled(bool enabled) { enabled_ = enabled; }

const PitchHoldSettings &PitchHoldController::GetSettings() const {
  return settings_;
}

void PitchHoldController::SetSettings(const PitchHoldSettings &settings) {
  settings_ = settings;
}

double PitchHoldController::GetTrimElevator() const { return trimElevator_; }

void PitchHoldController::SetTrimElevator(double trimElevator) {
  trimElevator_ = trimElevator;
}

std::optional<double> PitchHoldController::OnTick(const sim::Aircraft &aircraft,
    const sim::Tick &, const ControlContext &context) {
  if (!enabled_) {
    return std::nullopt;
  }

  const sim::jsbsim::Properties &prop = aircraft.GetProperties();
  const auto &[aTheta1, aTheta2, aTheta3] = *context.pitchDynamics;

  const double wN = settings_.naturalFrequencyRadPerSec;
  const double zeta = settings_.dampingRatio;

  const double kP = (wN * wN - aTheta2) / aTheta3;
  const double kD = (2 * zeta * wN - aTheta1) / aTheta3;

  const double error = settings_.targetPitchRad - prop.Pitch().Rad();
  const double newElevator =
      GetTrimElevator() + kP * error - kD * prop.Q().RadPerSec();

  return newElevator;
}

} // namespace gnc
