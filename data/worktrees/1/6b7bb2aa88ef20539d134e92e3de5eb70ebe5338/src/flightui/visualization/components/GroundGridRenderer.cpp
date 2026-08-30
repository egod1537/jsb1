#include "flightui/visualization/components/GroundGridRenderer.hpp"

#include "flightui/visualization/core/Transform.hpp"
#include "flightui/visualization/render/LineCanvas.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr float MinimumVisualAltitude = 0.35F;
constexpr float AltitudeSmoothing = 0.18F;
constexpr float LodCoarsenRatio = 1.0F;
constexpr float LodRefineRatio = 0.62F;

float SanitizeAltitude(float visualAltitude) {
  if (!std::isfinite(visualAltitude)) {
    return MinimumVisualAltitude;
  }

  return std::max(visualAltitude, MinimumVisualAltitude);
}

float SmoothAltitude(float currentAltitude, float targetAltitude) {
  return currentAltitude + (targetAltitude - currentAltitude)
                               * AltitudeSmoothing;
}

float ResolveGridSpacing(float extent, float baseSpacing, float currentSpacing,
    int maxLinesPerAxis) {
  float spacing = std::max(currentSpacing, baseSpacing);
  const float maxLines = static_cast<float>(maxLinesPerAxis);

  while ((extent * 2.0F) / spacing > maxLines * LodCoarsenRatio) {
    spacing *= 2.0F;
  }

  while (spacing > baseSpacing
         && (extent * 2.0F) / (spacing * 0.5F)
                < maxLines * LodRefineRatio) {
    spacing *= 0.5F;
  }

  return spacing;
}

float WrapOffset(float offset, float repeatDistance) {
  if (repeatDistance <= 0.0001F || !std::isfinite(offset)) {
    return 0.0F;
  }

  float wrapped = std::fmod(offset, repeatDistance);
  if (wrapped > repeatDistance * 0.5F) {
    wrapped -= repeatDistance;
  } else if (wrapped < -repeatDistance * 0.5F) {
    wrapped += repeatDistance;
  }

  return wrapped;
}

int FadeAlpha(float offset, float extent, int nearAlpha, int farAlpha) {
  const float distance = std::fabs(offset);
  const float fade = 1.0F - std::clamp(distance / extent, 0.0F, 1.0F);
  return static_cast<int>(static_cast<float>(farAlpha)
                          + fade * static_cast<float>(nearAlpha - farAlpha));
}
} // namespace

namespace viz {
void GroundGridRenderer::OnTick(const TickContext &context) {
  const AircraftSnapshot &aircraft = context.snapshot.aircraft;
  const float targetAltitude =
      SanitizeAltitude(aircraft.visualAltitude);
  if (!hasVisualAltitude_) {
    visualAltitude_ = targetAltitude;
    hasVisualAltitude_ = true;
  } else {
    visualAltitude_ = SmoothAltitude(visualAltitude_, targetAltitude);
  }

  extent_ = std::clamp(visualAltitude_ * 14.0F, 32.0F, 280.0F);
  spacing_ =
      ResolveGridSpacing(extent_, baseSpacing_, spacing_, maxLinesPerAxis_);
  const float repeatDistance =
      spacing_ * static_cast<float>(majorLineInterval_);
  GetTransform().SetPosition({
      aircraft.position.x
          + WrapOffset(context.snapshot.groundScroll.x, repeatDistance),
      aircraft.position.y
          + WrapOffset(context.snapshot.groundScroll.y, repeatDistance),
      aircraft.position.z - visualAltitude_,
  });
}

void GroundGridRenderer::Render(RenderContext &context) const {
  if (!context.snapshot.viewOptions.showGroundGrid) {
    return;
  }

  const int halfLineCount =
      static_cast<int>(std::ceil(extent_ / spacing_));
  const float lineExtent = static_cast<float>(halfLineCount) * spacing_;
  const Transform &transform = GetTransform();

  for (int lineIndex = -halfLineCount; lineIndex <= halfLineCount;
       ++lineIndex) {
    const float offset = static_cast<float>(lineIndex) * spacing_;
    const bool isMajor = lineIndex % majorLineInterval_ == 0;
    const int alpha = FadeAlpha(offset, lineExtent, 150, 36);
    const ImU32 color =
        isMajor ? IM_COL32(96, 114, 132, std::max(alpha, 82))
                : IM_COL32(62, 76, 90, alpha);
    const float thickness = isMajor ? 1.25F : 1.0F;

    context.canvas.Line(transform.TransformPoint({offset, -lineExtent, 0.0F}),
        transform.TransformPoint({offset, lineExtent, 0.0F}),
        color,
        thickness);
    context.canvas.Line(transform.TransformPoint({-lineExtent, offset, 0.0F}),
        transform.TransformPoint({lineExtent, offset, 0.0F}),
        color,
        thickness);
  }
}
} // namespace viz
