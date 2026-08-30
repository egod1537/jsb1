#pragma once

#include "gui/Window.hpp"

namespace gui {
class ScenarioWindow final : public Window {
public:
  ScenarioWindow();

protected:
  // Window configuration and rendering
  void PrepareWindow() override;
  void OnRender(const sim::SimulationSnapshot &snapshot) override;
};
} // namespace gui
