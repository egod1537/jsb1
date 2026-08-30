#include "gui/features/monitor/MonitorController.hpp"
#include "sim/linearization/DynamicModeContracts.hpp"
#include "sim/telemetry/TelemetryContracts.hpp"

#include <cassert>
#include <cmath>
#include <memory>

namespace {
constexpr double Tolerance = 1.0e-9;

void RequireNear(double actual, double expected) {
  assert(std::abs(actual - expected) <= Tolerance);
}

gui::MonitorController MakeControllerWithRange(double minimum, double maximum) {
  gui::MonitorController controller;
  controller.Handle(gui::MonitorTelemetryRangeChanged{{minimum, maximum}});
  return controller;
}

void TestLiveTelemetryExtendsSharedRanges() {
  gui::MonitorController controller = MakeControllerWithRange(0.0, 100.0);
  const gui::MonitorTimelineState &timeline = controller.GetState().timeline;
  assert(timeline.live);
  RequireNear(timeline.totalRange.maxSec, 100.0);
  RequireNear(timeline.viewRange.minSec, 60.0);
  RequireNear(timeline.viewRange.maxSec, 100.0);
  RequireNear(timeline.visibleRange.minSec, 90.0);
  RequireNear(timeline.visibleRange.maxSec, 100.0);
  RequireNear(timeline.cursorTimeSec, 100.0);
}

void TestDisablingLiveFreezesExpectedRange() {
  gui::MonitorController controller = MakeControllerWithRange(0.0, 100.0);
  controller.Handle(gui::MonitorLiveChanged{false});
  const gui::MonitorTimeRange frozen =
      controller.GetState().timeline.visibleRange;

  controller.Handle(gui::MonitorTelemetryRangeChanged{{0.0, 120.0}});

  assert(!controller.GetState().timeline.live);
  RequireNear(controller.GetState().timeline.visibleRange.minSec,
      frozen.minSec);
  RequireNear(controller.GetState().timeline.visibleRange.maxSec,
      frozen.maxSec);
  RequireNear(controller.GetState().timeline.totalRange.maxSec, 120.0);
}

void TestZoomUpdatesSharedTimeline() {
  gui::MonitorController controller = MakeControllerWithRange(0.0, 100.0);
  controller.Handle(gui::MonitorLiveChanged{false});
  const double oldDuration = controller.GetState().timeline.viewRange.maxSec
                             - controller.GetState().timeline.viewRange.minSec;

  controller.Handle(gui::MonitorZoomRequested{1.0, 80.0});

  const gui::MonitorTimelineState &timeline = controller.GetState().timeline;
  const double newDuration =
      timeline.viewRange.maxSec - timeline.viewRange.minSec;
  assert(newDuration < oldDuration);
  assert(newDuration
         >= timeline.visibleRange.maxSec - timeline.visibleRange.minSec);
}

void TestPanUpdatesSharedRanges() {
  gui::MonitorController controller = MakeControllerWithRange(0.0, 200.0);
  controller.Handle(gui::MonitorLiveChanged{false});
  controller.Handle(gui::MonitorViewRangeChanged{{50.0, 90.0}});
  controller.Handle(gui::MonitorVisibleRangeChanged{{60.0, 70.0}});

  controller.Handle(gui::MonitorPanRequested{5.0});

  RequireNear(controller.GetState().timeline.viewRange.minSec, 55.0);
  RequireNear(controller.GetState().timeline.viewRange.maxSec, 95.0);
  RequireNear(controller.GetState().timeline.visibleRange.minSec, 65.0);
  RequireNear(controller.GetState().timeline.visibleRange.maxSec, 75.0);
}

void TestCursorMovementPropagatesThroughController() {
  gui::MonitorController controller = MakeControllerWithRange(0.0, 100.0);
  controller.Handle(gui::MonitorCursorMoved{42.5});
  assert(controller.GetState().timeline.cursorInitialized);
  RequireNear(controller.GetState().timeline.cursorTimeSec, 42.5);
}

void TestEveryPlotReceivesTheSameTimelineProps() {
  gui::MonitorController controller;
  gui::MonitorState state = controller.GetState();
  state.plots.push_back({.id = 1, .title = "Roll"});
  state.plots.push_back({.id = 2, .title = "Pitch"});
  controller.Handle(gui::MonitorStateChanged{state});

  const std::vector<gui::MonitorPlotProps> props = controller.BuildPlotProps();
  assert(props.size() == 2);
  assert(props[0].timeline == props[1].timeline);
  assert(props[0].timeline == &controller.GetState().timeline);
}

void TestInputUsesProvidedSnapshotDataOnly() {
  auto telemetry = std::make_shared<telemetry::TelemetrySnapshot>();
  telemetry->available = true;
  telemetry->publishedTimeRange = telemetry::TelemetryTimeRange{2.0, 25.0};
  const std::vector<gnc::DynamicModeSnapshot> dynamicModes(1);
  gui::MonitorController controller;

  controller.SetInput({
      .primary = telemetry,
      .dynamicModes = {.history = dynamicModes, .available = true},
  });

  assert(controller.GetInput().primary == telemetry);
  assert(
      controller.GetInput().dynamicModes.history.data() == dynamicModes.data());
  RequireNear(controller.GetState().timeline.totalRange.minSec, 0.0);
  RequireNear(controller.GetState().timeline.totalRange.maxSec, 25.0);
}

void TestApplicationIntentIsEmittedUpward() {
  bool enabled = false;
  gui::MonitorController controller(
      gui::architecture::EventSink<gui::MonitorAutomaticLinearizationChanged>{
          [&enabled](const auto &event) { enabled = event.enabled; }});

  controller.Handle(gui::MonitorAutomaticLinearizationChanged{true});

  assert(enabled);
}
} // namespace

int main() {
  TestLiveTelemetryExtendsSharedRanges();
  TestDisablingLiveFreezesExpectedRange();
  TestZoomUpdatesSharedTimeline();
  TestPanUpdatesSharedRanges();
  TestCursorMovementPropagatesThroughController();
  TestEveryPlotReceivesTheSameTimelineProps();
  TestInputUsesProvidedSnapshotDataOnly();
  TestApplicationIntentIsEmittedUpward();
  return 0;
}
