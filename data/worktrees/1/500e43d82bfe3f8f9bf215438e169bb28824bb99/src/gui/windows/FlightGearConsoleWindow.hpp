#pragma once

#include "gui/Window.hpp"
class FlightGearConsoleWindow final : public gui::Window {
public:
  FlightGearConsoleWindow();

protected:
  void OnRender(const sim::SimulationSnapshot &snapshot) override;
};
