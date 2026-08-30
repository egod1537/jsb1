#include "gui/architecture/Feature.hpp"
#include "gui/architecture/View.hpp"
#include "gui/features/autopilot_selector/AutopilotSelectorController.hpp"

#include <cassert>
#include <optional>

namespace {
static_assert(gui::architecture::ViewFor<gui::AutopilotSelectorView,
    gui::AutopilotViewState, gui::AutopilotSelectorProps,
    gui::AutopilotSourceSelected>);
static_assert(gui::architecture::FeatureFor<gui::AutopilotSelectorController,
    gui::AutopilotSelectorProps>);

void TestInteractionUpdatesModelAndEmitsSemanticEvent() {
  std::optional<gui::AutopilotSelectionChanged> parentEvent;
  gui::AutopilotSelectorController controller(
      gui::architecture::EventSink<gui::AutopilotSelectionChanged>{
          [&parentEvent](const gui::AutopilotSelectionChanged &event) {
            parentEvent = event;
          }});

  controller.HandleEvent({gui::AutopilotSelection::Baseline},
      {.baselineAvailable = true});

  assert(controller.GetModel().GetSelection()
         == gui::AutopilotSelection::Baseline);
  assert(parentEvent.has_value());
  assert(parentEvent->selection == gui::AutopilotSelection::Baseline);
}

void TestUnavailableSelectionIsRejected() {
  int eventCount = 0;
  gui::AutopilotSelectorController controller(
      gui::architecture::EventSink<gui::AutopilotSelectionChanged>{
          [&eventCount](
              const gui::AutopilotSelectionChanged &) { ++eventCount; }});

  controller.HandleEvent({gui::AutopilotSelection::Baseline},
      {.baselineAvailable = false});

  assert(
      controller.GetModel().GetSelection() == gui::AutopilotSelection::Primary);
  assert(eventCount == 0);
}

void TestAuthoritativePropsForceSafeSelection() {
  std::optional<gui::AutopilotSelectionChanged> parentEvent;
  gui::AutopilotSelectorController controller(
      gui::architecture::EventSink<gui::AutopilotSelectionChanged>{
          [&parentEvent](const gui::AutopilotSelectionChanged &event) {
            parentEvent = event;
          }});
  controller.HandleEvent({gui::AutopilotSelection::Baseline},
      {.baselineAvailable = true});
  parentEvent.reset();

  controller.Update({.baselineAvailable = false});

  assert(
      controller.GetModel().GetSelection() == gui::AutopilotSelection::Primary);
  assert(parentEvent.has_value());
  assert(parentEvent->selection == gui::AutopilotSelection::Primary);
}
} // namespace

int main() {
  TestInteractionUpdatesModelAndEmitsSemanticEvent();
  TestUnavailableSelectionIsRejected();
  TestAuthoritativePropsForceSafeSelection();
  return 0;
}
