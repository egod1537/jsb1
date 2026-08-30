#include "gui/windows/FlightConsoleWindow.hpp"
#include "flightui/FlightUI.hpp"
#include "sim/SimulationConfig.h"
#include "sim/runtime/SimulationContracts.hpp"

namespace gui {
namespace UI = FlightUI;

FlightConsoleWindow::FlightConsoleWindow() : Window("Flight Console") {}

void FlightConsoleWindow::OnRender(const sim::SimulationSnapshot &snapshot) {
  const sim::SimulationConfig &config = snapshot.config;
  const sim::InitialCondition &initialCondition =
      snapshot.defaultInitialCondition;

  // clang-format off
  FlightUI::UIElement content =
      UI::VerticalLayout()
      [
        +UI::Heading("JSB Flight Console")
        + UI::Text("Aircraft: " + config.aircraftName)
        + UI::ValueLabel("Simulation", config.simulationHz, "%.1f Hz")
        + UI::ValueLabel("Initial altitude", initialCondition.altitudeFt, "%.0f ft")
        + UI::ValueLabel("Initial airspeed", initialCondition.airspeedKts,
                         "%.0f kt")
      ];
  // clang-format on

  content.Render();
}
} // namespace gui
