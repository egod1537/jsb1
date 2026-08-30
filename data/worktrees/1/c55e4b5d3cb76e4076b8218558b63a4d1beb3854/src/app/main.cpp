#include "app/Application.hpp"
#include "gui/GUI.hpp"
#include "gui/GUIConfig.hpp"
#include "sim/Simulation.hpp"
#include "sim/SimulationConfig.h"
#include "sim/execution/ExecutionVariantResolver.hpp"
#include "sim/runtime/SimulationRuntime.hpp"

#include <csignal>
#include <memory>

namespace {
volatile std::sig_atomic_t running = 1;
}

void HandleSignal(int) { running = 0; }

int main() {
  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  sim::SimulationConfig simConfig;
  gui::GUIConfig guiConfig;

  std::unique_ptr<sim::Simulation> primarySimulation =
      std::make_unique<sim::Simulation>(
          sim::ExecutionVariantResolver::CreateAutopilot(
              sim::ExecutionVariant::Primary));
  std::unique_ptr<sim::Simulation> baselineSimulation =
      std::make_unique<sim::Simulation>(
          sim::ExecutionVariantResolver::CreateAutopilot(
              sim::ExecutionVariant::Baseline));
  std::unique_ptr<gui::GUI> gui = std::make_unique<gui::GUI>(guiConfig);
  auto simulationRuntime =
      std::make_unique<sim::SimulationRuntime>(std::move(primarySimulation),
          std::move(baselineSimulation));

  Application app(std::move(gui), std::move(simulationRuntime), simConfig);
  return app.Run(running) ? 0 : 1;
}
