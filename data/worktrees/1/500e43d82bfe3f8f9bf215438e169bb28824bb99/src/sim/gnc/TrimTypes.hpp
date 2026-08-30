#pragma once

#include <string>

namespace gnc {
enum class TrimMode {
  Longitudinal,
  Full,
  Ground,
};

struct TrimRequest {
  TrimMode mode = TrimMode::Longitudinal;

  double airspeedKts = 100.0;
  double altitudeFt = 3000.0;
  double flightPathAngleDeg = 0.0;
};

struct TrimResult {
  bool success = false;
  std::string message;

  double alphaDeg = 0.0;
  double betaDeg = 0.0;
  double rollDeg = 0.0;
  double pitchDeg = 0.0;

  double throttle = 0.0;
  double elevator = 0.0;
  double pitchTrim = 0.0;
  double aileron = 0.0;
  double rudder = 0.0;

  double uDot = 0.0;
  double vDot = 0.0;
  double wDot = 0.0;

  double pDot = 0.0;
  double qDot = 0.0;
  double rDot = 0.0;
};
} // namespace gnc
