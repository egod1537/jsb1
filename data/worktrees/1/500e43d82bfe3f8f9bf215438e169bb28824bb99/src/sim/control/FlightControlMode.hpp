#pragma once

namespace control {
enum class FlightControlMode {
  None,
  Manual,
  Autopilot,
};

inline const char *ToString(FlightControlMode mode) {
  switch (mode) {
  case FlightControlMode::None:
    return "None";
  case FlightControlMode::Manual:
    return "Manual";
  case FlightControlMode::Autopilot:
    return "Autopilot";
  }

  return "Unknown";
}
} // namespace control
