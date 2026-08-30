#include "gui/features/monitor/MonitorController.hpp"

#include "sim/telemetry/TelemetryContracts.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>

namespace gui {
namespace {
constexpr double MinimumTimelineWindowSec = 0.1;
constexpr double TimelineZoomFactor = 1.15;

double Clamp(double value, double minimum, double maximum) {
  return std::clamp(value,
      std::min(minimum, maximum),
      std::max(minimum, maximum));
}

MonitorTimeRange EffectiveRange(MonitorTimeRange range) {
  if (range.maxSec - range.minSec < MinimumTimelineWindowSec) {
    range.maxSec = range.minSec + MinimumTimelineWindowSec;
  }
  return range;
}
} // namespace

MonitorController::MonitorController(
    architecture::EventSink<MonitorAutomaticLinearizationChanged> parentEvents)
    : parentEvents_(std::move(parentEvents)) {}

void MonitorController::SetInput(MonitorInput input) {
  if (input.primary != nullptr && !input.primary->available) {
    input.primary.reset();
  }
  if (input.baseline != nullptr && !input.baseline->available) {
    input.baseline.reset();
  }
  input_ = std::move(input);

  if (input_.primary == nullptr
      || !input_.primary->publishedTimeRange.has_value()) {
    state_.timeline.historyRange.reset();
    state_.timeline.cursorInitialized = false;
    return;
  }
  const telemetry::TelemetryTimeRange &range =
      *input_.primary->publishedTimeRange;
  Handle(MonitorTelemetryRangeChanged{
      {std::min(0.0, range.minSec), range.maxSec}});
}

std::vector<MonitorPlotProps> MonitorController::BuildPlotProps() const {
  std::vector<MonitorPlotProps> props;
  props.reserve(state_.plots.size());
  for (const MonitorPlotState &plot : state_.plots) {
    props.push_back({&plot, &state_.timeline});
  }
  return props;
}

void MonitorController::Handle(const MonitorEvent &event) {
  std::visit([this](const auto &typedEvent) { Handle(typedEvent); }, event);
}

void MonitorController::Handle(const MonitorLiveChanged &event) {
  state_.timeline.live = event.enabled;
  if (event.enabled) {
    UpdateLiveRanges();
    state_.timeline.cursorTimeSec = state_.timeline.totalRange.maxSec;
    state_.timeline.cursorInitialized = true;
  }
}

void MonitorController::Handle(const MonitorViewRangeChanged &event) {
  state_.timeline.viewRange = event.range;
  state_.timeline.viewWindowSec = event.range.maxSec - event.range.minSec;
  ClampViewRange();
}

void MonitorController::Handle(const MonitorVisibleRangeChanged &event) {
  state_.timeline.visibleRange = event.range;
  state_.timeline.liveWindowSec = event.range.maxSec - event.range.minSec;
  ClampVisibleRange();
}

void MonitorController::Handle(const MonitorCursorMoved &event) {
  state_.timeline.cursorTimeSec = Clamp(event.timeSec,
      state_.timeline.totalRange.minSec,
      state_.timeline.totalRange.maxSec);
  state_.timeline.cursorInitialized = true;
}

void MonitorController::Handle(const MonitorSelectedRangeChanged &event) {
  state_.timeline.selectedRange = event.range;
}

void MonitorController::Handle(const MonitorZoomRequested &event) {
  if (!std::isfinite(event.wheelDelta) || event.wheelDelta == 0.0) {
    return;
  }
  const MonitorTimeRange total = EffectiveRange(state_.timeline.totalRange);
  const double historyDuration = total.maxSec - total.minSec;
  const double visibleDuration =
      state_.timeline.visibleRange.maxSec - state_.timeline.visibleRange.minSec;
  const double minimumDuration = std::min(historyDuration,
      std::max(MinimumTimelineWindowSec, visibleDuration));
  const double currentDuration =
      Clamp(state_.timeline.viewRange.maxSec - state_.timeline.viewRange.minSec,
          MinimumTimelineWindowSec,
          historyDuration);
  const double newDuration =
      Clamp(currentDuration * std::pow(TimelineZoomFactor, -event.wheelDelta),
          minimumDuration,
          historyDuration);
  state_.timeline.viewWindowSec = newDuration;
  if (state_.timeline.live) {
    state_.timeline.viewRange = {total.maxSec - newDuration, total.maxSec};
    return;
  }
  const double anchorRatio =
      (event.anchorSec - state_.timeline.viewRange.minSec) / currentDuration;
  state_.timeline.viewRange.minSec =
      event.anchorSec - newDuration * anchorRatio;
  state_.timeline.viewRange.maxSec =
      state_.timeline.viewRange.minSec + newDuration;
  ClampViewRange();
}

void MonitorController::Handle(const MonitorPanRequested &event) {
  if (!std::isfinite(event.deltaSec) || state_.timeline.live) {
    return;
  }
  state_.timeline.viewRange.minSec += event.deltaSec;
  state_.timeline.viewRange.maxSec += event.deltaSec;
  state_.timeline.visibleRange.minSec += event.deltaSec;
  state_.timeline.visibleRange.maxSec += event.deltaSec;
  ClampViewRange();
  ClampVisibleRange();
}

void MonitorController::Handle(const MonitorTelemetryRangeChanged &event) {
  state_.timeline.totalRange = event.range;
  state_.timeline.historyRange = event.range;
  if (state_.timeline.live) {
    UpdateLiveRanges();
    state_.timeline.cursorTimeSec = event.range.maxSec;
    state_.timeline.cursorInitialized = true;
  } else {
    ClampViewRange();
    ClampVisibleRange();
  }
}

void MonitorController::Handle(const MonitorStateChanged &event) {
  state_ = event.state;
}

void MonitorController::Handle(
    const MonitorAutomaticLinearizationChanged &event) {
  parentEvents_.Emit(event);
}

void MonitorController::UpdateLiveRanges() {
  const MonitorTimeRange total = EffectiveRange(state_.timeline.totalRange);
  const double historyDuration = total.maxSec - total.minSec;
  const double visibleDuration =
      std::min(state_.timeline.liveWindowSec, historyDuration);
  const double viewDuration =
      std::min(std::max(state_.timeline.viewWindowSec, visibleDuration),
          historyDuration);
  state_.timeline.viewRange = {total.maxSec - viewDuration, total.maxSec};
  state_.timeline.visibleRange = {total.maxSec - visibleDuration, total.maxSec};
}

void MonitorController::ClampViewRange() {
  const MonitorTimeRange total = EffectiveRange(state_.timeline.totalRange);
  const double historyDuration = total.maxSec - total.minSec;
  double duration =
      state_.timeline.viewRange.maxSec - state_.timeline.viewRange.minSec;
  if (!std::isfinite(duration) || duration < MinimumTimelineWindowSec) {
    duration = MinimumTimelineWindowSec;
  }
  duration = std::min(duration, historyDuration);
  const double minimum = Clamp(state_.timeline.viewRange.minSec,
      total.minSec,
      total.maxSec - duration);
  state_.timeline.viewRange = {minimum, minimum + duration};
}

void MonitorController::ClampVisibleRange() {
  const MonitorTimeRange total = EffectiveRange(state_.timeline.totalRange);
  const double historyDuration = total.maxSec - total.minSec;
  double duration =
      state_.timeline.visibleRange.maxSec - state_.timeline.visibleRange.minSec;
  if (!std::isfinite(duration) || duration < MinimumTimelineWindowSec) {
    duration = MinimumTimelineWindowSec;
  }
  duration = std::min(duration, historyDuration);
  const double minimum = Clamp(state_.timeline.visibleRange.minSec,
      total.minSec,
      total.maxSec - duration);
  state_.timeline.visibleRange = {minimum, minimum + duration};
}
} // namespace gui
