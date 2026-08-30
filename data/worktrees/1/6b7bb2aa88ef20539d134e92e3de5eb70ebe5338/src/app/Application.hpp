#pragma once

#include "integration/flightgear/FlightGearSystem.hpp"
#include "messaging/MessageBus.hpp"
#include "sim/SimulationConfig.h"

#include <csignal>
#include <cstdint>
#include <memory>

namespace sim {
class SimulationRuntime;
}
namespace gui {
class GUI;
}
namespace application {
class SimulationMessageClient;
namespace messaging {
class SimulationMessageAdapter;
}
} // namespace application

class Application {
public:
  // Lifetime and main loop
  Application();
  ~Application();
  Application(std::unique_ptr<gui::GUI> gui,
      std::unique_ptr<sim::SimulationRuntime> simulationRuntime,
      sim::SimulationConfig simConfig);
  bool Run(const volatile std::sig_atomic_t &running);

private:
  // Application lifecycle
  bool Start();
  bool RunMainLoop(const volatile std::sig_atomic_t &running);
  bool TickSimulation();
  void TickGUI();
  void Exit();

  // Owned services
  application::messaging::MessageBus messageBus_;
  std::unique_ptr<sim::SimulationRuntime> simulationRuntime_;
  std::unique_ptr<application::messaging::SimulationMessageAdapter>
      simulationMessageAdapter_;
  std::unique_ptr<application::SimulationMessageClient>
      simulationMessageClient_;
  std::unique_ptr<gui::GUI> gui_;
  flightgear::FlightGearSystem flightGear_;

  // Configuration
  sim::SimulationConfig simConfig_;
};
