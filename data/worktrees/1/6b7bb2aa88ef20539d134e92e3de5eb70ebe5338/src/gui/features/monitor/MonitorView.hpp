#pragma once

#include "gui/architecture/EventSink.hpp"
#include "gui/features/monitor/MonitorEvents.hpp"
#include "gui/features/monitor/MonitorInput.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace telemetry {
struct TelemetrySnapshot;
}

namespace gui {
class MonitorView {
public:
  MonitorView();

  void Render(const MonitorInput &input, const MonitorState &state,
      architecture::EventSink<MonitorEvent> events);

private:
  using TelemetrySources = MonitorInput;
  using MonitorPlot = MonitorPlotState;
  using TimelineRange = MonitorTimeRange;
  using TimelineDragMode = MonitorTimelineDragMode;
  using TimelineDragTarget = MonitorTimelineDragTarget;

  struct BrowserNode {
    std::string name;
    std::string fullPath;
    std::map<std::string, BrowserNode, std::less<>> children;
    bool isChannel = false;
  };

  // Workspace setup and plot management
  void CreateDefaultPreset();
  MonitorPlot &CreatePlot(std::string title,
      std::string telemetryGroupPath = {}, std::string yAxisLabel = "Value");
  MonitorPlot *FindBoundPlot(std::string_view telemetryNodePath);
  void DeletePlot(std::uint64_t plotId);
  void SetChannelEnabled(MonitorPlot &plot, std::string_view channelPath,
      bool enabled);

  // Workspace rendering
  void DrawWindow(const TelemetrySources &sources,
      std::span<const gnc::DynamicModeSnapshot> dynamicModeHistory);
  void DrawDynamicModes(const MonitorDynamicModeInput &dynamicModes);
  void DrawToolbar(const telemetry::TelemetrySnapshot &telemetry);
  void DrawExplorerHeader();
  void DrawTelemetryBrowser(const telemetry::TelemetrySnapshot &telemetry);
  void AddBrowserPath(BrowserNode &root, std::string_view path) const;
  void DrawBrowserNode(const BrowserNode &node, bool expandAll);
  void DrawBrowserChannel(std::string_view label, std::string_view channelPath);
  void DrawPresetPanel();
  void DrawPlotWorkspace(const TelemetrySources &sources,
      std::span<const gnc::DynamicModeSnapshot> dynamicModeHistory);
  void DrawPlotScrollRegion(const TelemetrySources &sources);
  void DrawTimelineHeader();
  void DrawTimeline(
      std::span<const gnc::DynamicModeSnapshot> dynamicModeHistory);
  void DrawTimelineOverview(const TimelineRange &historyRange);
  void DrawTimelineDetail();
  void DrawLinearizationTrack(
      std::span<const gnc::DynamicModeSnapshot> dynamicModeHistory);
  void DrawPlotLayoutSelector();
  void DrawPlotList(const TelemetrySources &sources);
  void DrawPlotGrid(const TelemetrySources &sources, int dimension);
  void DrawPlotTable(const TelemetrySources &sources, int columnCount,
      float plotHeight, const char *tableId);
  float CalculateGridPlotHeight(int rowCount) const;
  bool DrawPlotCard(MonitorPlot &plot, const TelemetrySources &sources,
      float plotHeight);
  void DrawTelemetryPlot(const MonitorPlot &plot,
      const TelemetrySources &sources, float plotHeight);
  void DrawRollTrackingAcceptanceUnderlay(const MonitorPlot &plot,
      const telemetry::TelemetrySnapshot &telemetry,
      std::size_t maximumRenderedSampleCount) const;

  // Visibility composition
  bool IsPlotVisible(const MonitorPlot &plot) const;
  bool IsPlotVisibleByPreset(const MonitorPlot &plot) const;
  bool IsPresetActive(std::size_t presetIndex) const;
  void SetPresetActive(std::size_t presetIndex, bool active);

  // Shared viewport and cursor
  std::optional<TimelineRange> GetTelemetryHistoryRange(
      const telemetry::TelemetrySnapshot &telemetry) const;
  void SynchronizeTimelineState(const telemetry::TelemetrySnapshot &telemetry);
  TimelineRange GetEffectiveHistoryRange(
      const TimelineRange &historyRange) const;
  void ClampTimelineViewRangeToHistory();
  void ClampVisibleTimeRangeToHistory();
  void EnsureVisibleTimeRangeInTimelineView();
  void UpdateSharedXAxisTicks();
  void UpdateLiveTimeRanges();
  void SetLiveView(bool enabled);
  void SelectTimelineTime(double timeSec, bool disableLive);
  void ZoomTimelineView(double wheelDelta, double anchorSec);
  void DrawPlotOverlay(const MonitorPlot &plot,
      const telemetry::TelemetrySnapshot &telemetry);

  // Per-frame render state. The controller remains the authoritative owner.
  MonitorState renderState_;
  architecture::EventSink<MonitorEvent> events_;

  MonitorTimelineState &timelineModel_;
  TimelineRange &timelineViewRange_;
  TimelineRange &visibleTimeRange_;
  std::optional<TimelineRange> &telemetryHistoryRange_;
  std::vector<double> &sharedXAxisTicks_;
  double &timelineViewWindowSec_;
  double &liveWindowSec_;
  double &selectedTimeSec_;
  bool &liveView_;
  bool &selectedTimeInitialized_;

  std::vector<MonitorPlot> &plots_;
  std::uint64_t &nextPlotId_;
  std::uint64_t &selectedPlotId_;
  MonitorPlotLayout &plotLayout_;
  std::uint32_t &activePresetMask_;

  float &explorerPaneWidth_;
  float &timelinePaneHeight_;
  bool &explorerPaneOpen_;
  bool &timelinePaneOpen_;

  std::array<char, 128> &channelSearch_;
  std::string &selectedChannelPath_;

  TimelineDragMode &timelineDragMode_;
  TimelineDragTarget &timelineDragTarget_;
  TimelineRange &timelineDragInitialRange_;
  TimelineRange &timelineDragAxisRange_;
  double &timelineDragAnchorSec_;
  std::optional<double> &linearizationTrackSnapTimeSec_;

  std::optional<std::size_t> &selectedDynamicModeIndex_;
  std::optional<double> &selectedDynamicModeSnapshotTimeSec_;
};
} // namespace gui
