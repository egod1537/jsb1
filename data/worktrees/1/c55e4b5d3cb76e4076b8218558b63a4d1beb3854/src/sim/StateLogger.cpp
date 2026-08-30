#include "sim/StateLogger.hpp"

#include "sim/Aircraft.hpp"
#include "sim/AircraftState.hpp"

#include <iostream>

namespace {
constexpr double LogIntervalSec = 1.0;
}

namespace sim {
bool StateLogger::OnInitialize() {
  ResetLogTimer();
  return true;
}

bool StateLogger::OnReset() {
  ResetLogTimer();
  return true;
}

bool StateLogger::OnPostTick(const Tick &) {
  const AircraftState state = GetAircraft().GetAircraftState();
  if (state.simulationTimeSec >= nextLogTime_) {
    PrintState();
    nextLogTime_ += LogIntervalSec;
  }

  return true;
}

void StateLogger::ResetLogTimer() { nextLogTime_ = 0.0; }

void StateLogger::PrintState() const {
  const AircraftState state = GetAircraft().GetAircraftState();
  std::cout << "t=" << state.simulationTimeSec
            << " s, altitude=" << state.altitudeAglFt
            << " ft, airspeed=" << state.calibratedAirspeedKts
            << " kt, pitch=" << state.pitchDeg << " deg\n";
}
} // namespace sim
