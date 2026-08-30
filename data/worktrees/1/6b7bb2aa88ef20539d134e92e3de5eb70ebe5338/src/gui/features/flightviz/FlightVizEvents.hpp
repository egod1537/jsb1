#pragma once

namespace gui {
struct FlightVizShadowVisibilityChanged {
  bool enabled = false;
};

struct FlightVizCameraViewToggleRequested {};

struct FlightVizDisplayOptionsChanged {
  bool showGroundGrid = true;
  bool showTelemetry = true;
  bool showMinimap = true;
};

struct FlightVizClearPathRequested {};
} // namespace gui
