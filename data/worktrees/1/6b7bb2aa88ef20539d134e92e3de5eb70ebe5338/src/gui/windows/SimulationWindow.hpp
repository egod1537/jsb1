#pragma once

#include "gui/features/simulation/SimulationController.hpp"
#include "gui/Window.hpp"

namespace FlightUI {
class UIElement;
}

namespace gui {
class SimulationWindow final : public gui::Window {
public:
  explicit SimulationWindow(SimulationController &controller);

protected:
  void OnRender(const sim::SimulationSnapshot &snapshot) override;

private:
  // Tab rendering
  void DrawInitialConditionTab(const sim::SimulationSnapshot &snapshot);
  void DrawDiagnosticsTab(const sim::SimulationSnapshot &snapshot);
  void DrawEnvironmentTab();
  void DrawAircraftTab(const sim::SimulationSnapshot &snapshot);

  // Initial-condition controls
  FlightUI::UIElement DrawInitialConditionFields();
  FlightUI::UIElement DrawInitialConditionActions(
      const sim::SimulationSnapshot &snapshot);

  // Simulation diagnostics
  FlightUI::UIElement DrawLastError(
      const sim::SimulationSnapshot &snapshot) const;

  SimulationController &controller_;
};
} // namespace gui
