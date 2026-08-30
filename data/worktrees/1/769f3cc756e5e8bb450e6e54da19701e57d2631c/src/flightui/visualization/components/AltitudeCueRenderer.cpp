#include "flightui/visualization/components/AltitudeCueRenderer.hpp"

#include "flightui/visualization/render/LineCanvas.hpp"
#include "flightui/core/UIScale.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr float MinimumVisualAltitude = 0.35F;
constexpr int GroundRingSegments = 28;

float SanitizeAltitude(float visualAltitude) {
  if (!std::isfinite(visualAltitude)) {
    return MinimumVisualAltitude;
  }

  return std::max(visualAltitude, MinimumVisualAltitude);
}

float ChooseTickSpacing(float visualAltitude) {
  float spacing = 1.0F;
  while (visualAltitude / spacing > 8.0F) {
    spacing *= 2.0F;
  }

  return spacing;
}

void DrawGroundRing(viz::LineCanvas &canvas, viz::Vec3 center, float radius,
    ImU32 color) {
  constexpr float TwoPi = 6.2831853071795864769F;

  for (int segmentIndex = 0; segmentIndex < GroundRingSegments;
      ++segmentIndex) {
    const float a0 = TwoPi * static_cast<float>(segmentIndex)
                     / static_cast<float>(GroundRingSegments);
    const float a1 = TwoPi * static_cast<float>(segmentIndex + 1)
                     / static_cast<float>(GroundRingSegments);
    const viz::Vec3 p0{
        center.x + std::cos(a0) * radius,
        center.y + std::sin(a0) * radius,
        center.z,
    };
    const viz::Vec3 p1{
        center.x + std::cos(a1) * radius,
        center.y + std::sin(a1) * radius,
        center.z,
    };
    canvas.Line(p0, p1, color, 1.2F);
  }
}
} // namespace

namespace viz {
void AltitudeCueRenderer::Render(RenderContext &context) const {
  const AircraftSnapshot &aircraft = context.snapshot.aircraft;
  const float visualAltitude = SanitizeAltitude(aircraft.visualAltitude);
  const Vec3 aircraftPoint = aircraft.position;
  const Vec3 groundPoint = aircraftPoint + Vec3{0.0F, 0.0F, -visualAltitude};
  const float markerRadius = std::clamp(visualAltitude * 0.12F, 0.55F, 3.0F);

  context.canvas.Line(aircraftPoint,
      groundPoint,
      IM_COL32(255, 207, 92, 230),
      2.0F);
  context.canvas.Line(groundPoint + Vec3{-markerRadius, 0.0F, 0.0F},
      groundPoint + Vec3{markerRadius, 0.0F, 0.0F},
      IM_COL32(255, 207, 92, 205),
      1.5F);
  context.canvas.Line(groundPoint + Vec3{0.0F, -markerRadius, 0.0F},
      groundPoint + Vec3{0.0F, markerRadius, 0.0F},
      IM_COL32(255, 207, 92, 205),
      1.5F);
  DrawGroundRing(context.canvas,
      groundPoint,
      markerRadius,
      IM_COL32(255, 207, 92, 118));

  const float tickSpacing = ChooseTickSpacing(visualAltitude);
  const int tickCount =
      static_cast<int>(std::floor(visualAltitude / tickSpacing));
  const float tickHalfWidth = std::clamp(visualAltitude * 0.018F, 0.16F, 0.55F);
  for (int tickIndex = 1; tickIndex < tickCount; ++tickIndex) {
    const float z =
        aircraftPoint.z - tickSpacing * static_cast<float>(tickIndex);
    const Vec3 tickCenter{aircraftPoint.x, aircraftPoint.y, z};
    context.canvas.Line(tickCenter + Vec3{-tickHalfWidth, 0.0F, 0.0F},
        tickCenter + Vec3{tickHalfWidth, 0.0F, 0.0F},
        IM_COL32(255, 226, 150, 185),
        1.0F);
  }

  const Vec3 labelPoint{
      aircraftPoint.x + markerRadius * 0.75F,
      aircraftPoint.y,
      aircraftPoint.z - visualAltitude * 0.5F,
  };
  const auto projectedLabel = context.canvas.ProjectPoint(labelPoint);
  if (!projectedLabel.has_value()) {
    return;
  }

  char label[96]{};
  std::snprintf(label,
      sizeof(label),
      "AGL %.0f ft",
      aircraft.state.altitudeAglFt);
  context.canvas.GetDrawList().AddText(
      ImVec2(projectedLabel->x + FlightUI::Ui(8.0F),
          projectedLabel->y - FlightUI::Ui(8.0F)),
      IM_COL32(255, 226, 150, 255),
      label);
}
} // namespace viz
