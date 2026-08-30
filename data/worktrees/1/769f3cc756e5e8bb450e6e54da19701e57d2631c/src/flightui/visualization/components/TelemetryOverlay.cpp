#include "flightui/visualization/components/TelemetryOverlay.hpp"

#include "flightui/visualization/render/LineCanvas.hpp"
#include "flightui/core/UIScale.hpp"

#include <cstdio>

namespace viz {
void TelemetryOverlay::Render(RenderContext &context) const {
  if (!context.snapshot.viewOptions.showTelemetry) {
    return;
  }

  const AircraftSnapshot &aircraft = context.snapshot.aircraft;
  const auto &aircraftState = aircraft.state;
  const auto &controlInput = aircraft.controlInput;
  const char *viewMode = context.snapshot.viewMode == ViewMode::ThirdPerson
                             ? "Third Person"
                             : "Orbit";
  const ImVec2 min = context.canvas.GetMin();
  ImDrawList &drawList = context.canvas.GetDrawList();

  char line[160]{};
  std::snprintf(line,
      sizeof(line),
      "t %.2f  View %s",
      aircraftState.simulationTimeSec,
      viewMode);
  drawList.AddText(
      ImVec2(min.x + FlightUI::Ui(10.0F), min.y + FlightUI::Ui(10.0F)),
      IM_COL32(232, 238, 246, 255),
      line);

  std::snprintf(line,
      sizeof(line),
      "Alt AGL %.0f ft  Course %.1f deg  CAS %.1f kt  TAS %.1f m/s",
      aircraftState.altitudeAglFt,
      aircraftState.courseDeg,
      aircraftState.calibratedAirspeedKts,
      aircraftState.trueAirspeedMps);
  drawList.AddText(
      ImVec2(min.x + FlightUI::Ui(10.0F), min.y + FlightUI::Ui(30.0F)),
      IM_COL32(232, 238, 246, 255),
      line);

  std::snprintf(line,
      sizeof(line),
      "Roll %.1f  Pitch %.1f  Heading %.1f",
      aircraftState.rollDeg,
      aircraftState.pitchDeg,
      aircraftState.headingDeg);
  drawList.AddText(
      ImVec2(min.x + FlightUI::Ui(10.0F), min.y + FlightUI::Ui(50.0F)),
      IM_COL32(232, 238, 246, 255),
      line);

  std::snprintf(line,
      sizeof(line),
      "Ail %.2f  Ele %.2f  Rud %.2f  Thr %.2f  Trim %.2f",
      controlInput.aileron,
      controlInput.elevator,
      controlInput.rudder,
      controlInput.throttle,
      aircraft.pitchTrim);
  drawList.AddText(
      ImVec2(min.x + FlightUI::Ui(10.0F), min.y + FlightUI::Ui(70.0F)),
      IM_COL32(178, 189, 202, 255),
      line);
}
} // namespace viz
