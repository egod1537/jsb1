#pragma once

#include "gui/features/autopilot_selector/AutopilotSelectorController.hpp"
#include "gui/features/gnc/GNCController.hpp"
#include "gui/panels/AutopilotPanel.hpp"
#include "gui/panels/BaselineAutopilotPanel.hpp"
#include "gui/Window.hpp"
#include "sim/gnc/TrimTypes.hpp"

namespace sim {
class Aircraft;
} // namespace sim

namespace gui {
class GNCWindow final : public gui::Window {
public:
  GNCWindow();
  explicit GNCWindow(GNCController &controller);

  AutopilotViewState &GetAutopilotViewState() {
    return autopilotSelector_.GetMutableModelForLegacy();
  }
  const AutopilotViewState &GetAutopilotViewState() const {
    return autopilotSelector_.GetModel();
  }

protected:
  void OnRender(const sim::SimulationSnapshot &snapshot) override;

private:
  // Dependencies
  GNCController *controller_ = nullptr;

  // Autopilot UI state
  AutopilotSelectorController autopilotSelector_;
};
} // namespace gui
