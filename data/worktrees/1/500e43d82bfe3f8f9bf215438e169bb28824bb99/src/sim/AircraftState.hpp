#pragma once

namespace sim {
struct AircraftState {
  double simulationTimeSec = 0.0;

  double altitudeAglFt = 0.0;
  double calibratedAirspeedKts = 0.0;
  double trueAirspeedMps = 0.0;

  double rollDeg = 0.0;
  double pitchDeg = 0.0;
  double headingDeg = 0.0;
  double courseDeg = 0.0;
  double alphaDeg = 0.0;
  double betaDeg = 0.0;

  double uMps = 0.0;
  double vMps = 0.0;
  double wMps = 0.0;

  double pDegPerSec = 0.0;
  double qDegPerSec = 0.0;
  double rDegPerSec = 0.0;
};

struct AircraftStateDerivative {
  double uDotMps2 = 0.0;
  double vDotMps2 = 0.0;
  double wDotMps2 = 0.0;

  double pDotDegPerSec2 = 0.0;
  double qDotDegPerSec2 = 0.0;
  double rDotDegPerSec2 = 0.0;
};
} // namespace sim
