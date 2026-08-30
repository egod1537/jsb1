#pragma once

#include "gui/architecture/Feature.hpp"
#include "gui/features/autopilot_selector/AutopilotSelectorView.hpp"

namespace gui {
class AutopilotSelectorController final
    : public architecture::FeatureController<AutopilotViewState,
          AutopilotSelectionChanged> {
public:
  explicit AutopilotSelectorController(
      architecture::EventSink<AutopilotSelectionChanged> parentEvents = {});

  // Temporary compatibility for the legacy GNCWindow public API. New feature
  // code must send an event instead of mutating this model directly.
  AutopilotViewState &GetMutableModelForLegacy() { return EditModel(); }

  // State/props flow
  void Update(const AutopilotSelectorProps &props);
  void Render(const AutopilotSelectorProps &props);

  // Interaction handling
  void HandleEvent(const AutopilotSourceSelected &event,
      const AutopilotSelectorProps &props);

private:
  void Select(AutopilotSelection selection,
      const AutopilotSelectorProps &props);

  AutopilotSelectorView view_;
};
} // namespace gui
