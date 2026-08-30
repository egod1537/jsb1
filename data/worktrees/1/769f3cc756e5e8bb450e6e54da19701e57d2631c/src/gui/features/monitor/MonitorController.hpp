#pragma once

#include "gui/architecture/EventSink.hpp"
#include "gui/features/monitor/MonitorEvents.hpp"
#include "gui/features/monitor/MonitorInput.hpp"

namespace gui {
class MonitorController {
public:
  explicit MonitorController(
      architecture::EventSink<MonitorAutomaticLinearizationChanged>
          parentEvents = {});

  // Passive snapshot input
  void SetInput(MonitorInput input);
  const MonitorInput &GetInput() const { return input_; }

  // Visualization state and rendering
  const MonitorState &GetState() const { return state_; }
  std::vector<MonitorPlotProps> BuildPlotProps() const;

  // Typed local interaction events
  void Handle(const MonitorEvent &event);
  void Handle(const MonitorLiveChanged &event);
  void Handle(const MonitorViewRangeChanged &event);
  void Handle(const MonitorVisibleRangeChanged &event);
  void Handle(const MonitorCursorMoved &event);
  void Handle(const MonitorSelectedRangeChanged &event);
  void Handle(const MonitorZoomRequested &event);
  void Handle(const MonitorPanRequested &event);
  void Handle(const MonitorTelemetryRangeChanged &event);
  void Handle(const MonitorStateChanged &event);
  void Handle(const MonitorAutomaticLinearizationChanged &event);

private:
  void UpdateLiveRanges();
  void ClampViewRange();
  void ClampVisibleRange();

  MonitorInput input_;
  MonitorState state_;
  architecture::EventSink<MonitorAutomaticLinearizationChanged> parentEvents_;
};
} // namespace gui
