#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gui {
struct MonitorTimeRange {
  double minSec = 0.0;
  double maxSec = 10.0;
};

struct MonitorTimelineState {
  bool live = true;
  bool cursorInitialized = false;
  MonitorTimeRange totalRange;
  std::optional<MonitorTimeRange> historyRange;
  MonitorTimeRange viewRange{0.0, 40.0};
  MonitorTimeRange visibleRange;
  std::optional<MonitorTimeRange> selectedRange;
  std::vector<double> sharedXAxisTicks;
  double viewWindowSec = 40.0;
  double liveWindowSec = 10.0;
  double cursorTimeSec = 0.0;
};

struct MonitorPlotState {
  std::uint64_t id = 0;
  std::string title;
  std::vector<std::string> channels;
  std::string telemetryGroupPath;
  std::string yAxisLabel = "Value";
  bool manualVisible = true;
};

struct MonitorPlotProps {
  const MonitorPlotState *plot = nullptr;
  const MonitorTimelineState *timeline = nullptr;
};

enum class MonitorPlotLayout {
  List,
  Grid2x2,
  Grid3x3,
};

enum class MonitorTimelineDragMode {
  None,
  Start,
  End,
  Window,
};

enum class MonitorTimelineDragTarget {
  None,
  TimelineView,
  PlotVisible,
};

struct MonitorState {
  MonitorTimelineState timeline;

  std::vector<MonitorPlotState> plots;
  std::uint64_t nextPlotId = 1;
  std::uint64_t selectedPlotId = 0;
  MonitorPlotLayout plotLayout = MonitorPlotLayout::Grid2x2;
  std::uint32_t activePresetMask = 0;

  float explorerPaneWidth = 270.0F;
  float timelinePaneHeight = 210.0F;
  bool explorerPaneOpen = true;
  bool timelinePaneOpen = true;

  std::array<char, 128> channelSearch{};
  std::string selectedChannelPath;

  MonitorTimelineDragMode timelineDragMode = MonitorTimelineDragMode::None;
  MonitorTimelineDragTarget timelineDragTarget =
      MonitorTimelineDragTarget::None;
  MonitorTimeRange timelineDragInitialRange;
  MonitorTimeRange timelineDragAxisRange;
  double timelineDragAnchorSec = 0.0;
  std::optional<double> linearizationTrackSnapTimeSec;

  std::optional<std::size_t> selectedDynamicModeIndex;
  std::optional<double> selectedDynamicModeSnapshotTimeSec;
  bool workspaceInitialized = false;
};
} // namespace gui
