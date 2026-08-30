#include "sim/gnc/hold/AirspeedHoldController.hpp"
#include "sim/Aircraft.hpp"
#include "sim/jsbsim/Properties.hpp"

namespace gnc {
void AirspeedHoldController::Reset() {}

bool AirspeedHoldController::IsEnabled() const { return enabled_; }

void AirspeedHoldController::SetEnabled(bool enabled) { enabled_ = enabled; }

const AirspeedHoldSettings &AirspeedHoldController::GetSettings() const {
  return settings_;
}

void AirspeedHoldController::SetSettings(const AirspeedHoldSettings &settings) {
  settings_ = settings;
}

double AirspeedHoldController::GetTrimThrottle() const { return trimThrottle_; }

void AirspeedHoldController::SetTrimThrottle(double trimThrottle) {
  trimThrottle_ = trimThrottle;
}

std::optional<double> AirspeedHoldController::OnTick(
    const sim::Aircraft &aircraft, const sim::Tick &) {
  if (!enabled_) {
    return std::nullopt;
  }

  const sim::jsbsim::Properties &prop = aircraft.GetProperties();

  const double error = settings_.targetAirspeedMps - prop.TrueAirspeed().Mps();
  const double newThrottle = GetTrimThrottle()
                             + settings_.proportionalGain * error
                             - settings_.derivativeGain * prop.U().DotMps2();

  return newThrottle;
}

} // namespace gnc
