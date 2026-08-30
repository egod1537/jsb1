#pragma once

#include "gui/features/monitor/MonitorModel.hpp"

#include <variant>

namespace gui {
struct MonitorLiveChanged {
  bool enabled = false;
};

struct MonitorViewRangeChanged {
  MonitorTimeRange range;
};

struct MonitorVisibleRangeChanged {
  MonitorTimeRange range;
};

struct MonitorCursorMoved {
  double timeSec = 0.0;
};

struct MonitorSelectedRangeChanged {
  std::optional<MonitorTimeRange> range;
};

struct MonitorZoomRequested {
  double wheelDelta = 0.0;
  double anchorSec = 0.0;
};

struct MonitorPanRequested {
  double deltaSec = 0.0;
};

struct MonitorTelemetryRangeChanged {
  MonitorTimeRange range;
};

struct MonitorStateChanged {
  MonitorState state;
};

struct MonitorAutomaticLinearizationChanged {
  bool enabled = false;
};

using MonitorEvent = std::variant<MonitorLiveChanged, MonitorViewRangeChanged,
    MonitorVisibleRangeChanged, MonitorCursorMoved, MonitorSelectedRangeChanged,
    MonitorZoomRequested, MonitorPanRequested, MonitorTelemetryRangeChanged,
    MonitorStateChanged, MonitorAutomaticLinearizationChanged>;
} // namespace gui
