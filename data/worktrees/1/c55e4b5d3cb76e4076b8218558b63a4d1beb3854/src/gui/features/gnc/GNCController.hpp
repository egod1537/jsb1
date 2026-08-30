#pragma once

#include "gui/features/gnc/GNCEvents.hpp"
#include "gui/panels/AutopilotPanel.hpp"
#include "gui/panels/BaselineAutopilotPanel.hpp"

namespace application {
class SimulationMessageClient;
}

namespace gui {
struct GNCModel {
  gnc::TrimRequest trimRequest;
  AutopilotPanelState primaryAutopilot;
  BaselineAutopilotPanelState baselineAutopilot;
  bool trimResultOpen = true;
  bool trimResidualOpen = true;
  bool trimInProgress = false;
  bool autopilotStateLoaded = false;
};

class GNCController {
public:
  explicit GNCController(application::SimulationMessageClient &client);

  const GNCModel &GetModel() const { return model_; }
  void Synchronize(const sim::SimulationSnapshot &snapshot);
  void PublishConfiguration(const sim::SimulationSnapshot &snapshot);

  void Handle(const TrimRequested &event);
  void Handle(const ManualControlChanged &event);
  void Handle(const PrimaryRollHoldConfigChanged &event);
  void Handle(const BaselineRollHoldConfigChanged &event);
  void Handle(const PrimaryRollHoldValueChanged &event);
  void Handle(const BaselineRollHoldValueChanged &event);
  void Handle(const BaselineRollHoldTuningResetRequested &event);
  void Handle(const TrimRequestValueChanged &event);
  void Handle(const TrimExecutionRequested &event);
  void Handle(const GNCViewStateChanged &event);

private:
  application::SimulationMessageClient &client_;
  GNCModel model_;
};
} // namespace gui
