#pragma once

#include "gui/Window.hpp"

namespace gui {
class FlightConsoleWindow final : public Window {
public:
  FlightConsoleWindow();

protected:
  void OnRender(const sim::SimulationSnapshot &snapshot) override;
};
} // namespace gui
