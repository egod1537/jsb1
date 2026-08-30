#include "gui/features/autopilot_selector/AutopilotSelectorController.hpp"

#include <utility>

namespace gui {
AutopilotSelectorController::AutopilotSelectorController(
    architecture::EventSink<AutopilotSelectionChanged> parentEvents)
    : FeatureController({}, std::move(parentEvents)) {}

void AutopilotSelectorController::Update(const AutopilotSelectorProps &props) {
  const AutopilotSelection previous = GetModel().GetSelection();
  EditModel().EnsureBaselineAvailable(props.baselineAvailable);
  const AutopilotSelection current = GetModel().GetSelection();
  if (current != previous) {
    EmitToParent({current});
  }
}

void AutopilotSelectorController::Render(const AutopilotSelectorProps &props) {
  Update(props);
  view_.Render(GetModel(),
      props,
      architecture::EventSink<AutopilotSourceSelected>{
          [this, props](const AutopilotSourceSelected &event) {
            HandleEvent(event, props);
          }});
}

void AutopilotSelectorController::HandleEvent(
    const AutopilotSourceSelected &event, const AutopilotSelectorProps &props) {
  Select(event.selection, props);
}

void AutopilotSelectorController::Select(AutopilotSelection selection,
    const AutopilotSelectorProps &props) {
  const AutopilotSelection previous = GetModel().GetSelection();
  if (!EditModel().Select(selection, props.baselineAvailable)
      || GetModel().GetSelection() == previous) {
    return;
  }
  EmitToParent({GetModel().GetSelection()});
}
} // namespace gui
