#pragma once

#include "integration/flightgear/FlightGearSender.hpp"

#include <memory>

namespace sim {
struct SimulationInstanceSnapshot;
}

namespace flightgear {
class FlightGearSystem {
public:
  bool Initialize();
  void Update(const sim::SimulationInstanceSnapshot &snapshot);
  void Shutdown();

private:
  std::unique_ptr<FlightGearSender> sender_;
};
} // namespace flightgear
