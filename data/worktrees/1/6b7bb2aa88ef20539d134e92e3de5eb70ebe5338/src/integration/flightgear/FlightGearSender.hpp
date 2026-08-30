#pragma once

#include <memory>

namespace sim {
struct SimulationInstanceSnapshot;
}

namespace flightgear {
class FlightGearSender {
public:
  FlightGearSender();
  ~FlightGearSender();

  FlightGearSender(const FlightGearSender &other) = delete;
  FlightGearSender &operator=(const FlightGearSender &other) = delete;

  bool IsOpen() const;
  bool Send(const sim::SimulationInstanceSnapshot &snapshot);

private:
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace flightgear
