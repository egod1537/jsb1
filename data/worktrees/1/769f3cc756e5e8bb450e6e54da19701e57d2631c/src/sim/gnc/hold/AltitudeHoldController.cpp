#include "sim/gnc/hold/AltitudeHoldController.hpp"

namespace gnc {
void AltitudeHoldController::Reset() {}

bool AltitudeHoldController::IsEnabled() const { return enabled_; }

void AltitudeHoldController::SetEnabled(bool enabled) { enabled_ = enabled; }

const AltitudeHoldSettings &AltitudeHoldController::GetSettings() const {
  return settings_;
}

void AltitudeHoldController::SetSettings(const AltitudeHoldSettings &settings) {
  settings_ = settings;
}

double AltitudeHoldController::GetTrimElevator() const { return trimElevator_; }

void AltitudeHoldController::SetTrimElevator(double trimElevator) {
  trimElevator_ = trimElevator;
}

} // namespace gnc
