#include "sim/InitialCondition.hpp"

#include <cmath>

namespace {
bool ValidationFailed(std::string *errorMessage, const char *message) {
  if (errorMessage != nullptr) {
    *errorMessage = message;
  }

  return false;
}
} // namespace

namespace sim {
bool ValidateInitialCondition(const InitialCondition &initialCondition,
    std::string *errorMessage) {
  if (errorMessage != nullptr) {
    errorMessage->clear();
  }

  if (!std::isfinite(initialCondition.latitudeDeg)
      || initialCondition.latitudeDeg < -90.0
      || initialCondition.latitudeDeg > 90.0) {
    return ValidationFailed(errorMessage,
        "Latitude must be finite and between -90 and 90 degrees.");
  }

  if (!std::isfinite(initialCondition.longitudeDeg)
      || initialCondition.longitudeDeg < -180.0
      || initialCondition.longitudeDeg > 180.0) {
    return ValidationFailed(errorMessage,
        "Longitude must be finite and between -180 and 180 degrees.");
  }

  if (!std::isfinite(initialCondition.altitudeFt)) {
    return ValidationFailed(errorMessage, "Altitude must be finite.");
  }

  if (!std::isfinite(initialCondition.rollDeg)
      || !std::isfinite(initialCondition.pitchDeg)
      || !std::isfinite(initialCondition.headingDeg)) {
    return ValidationFailed(errorMessage, "Attitude values must be finite.");
  }

  if (!std::isfinite(initialCondition.airspeedKts)
      || initialCondition.airspeedKts < 0.0) {
    return ValidationFailed(errorMessage,
        "Airspeed must be finite and non-negative.");
  }

  if (!std::isfinite(initialCondition.pRadPerSec)
      || !std::isfinite(initialCondition.qRadPerSec)
      || !std::isfinite(initialCondition.rRadPerSec)) {
    return ValidationFailed(errorMessage, "Angular rates must be finite.");
  }

  return true;
}
} // namespace sim
