#include "flightui/core/UIScale.hpp"

#include <algorithm>
#include <cmath>

namespace FlightUI {
namespace {
float uiScale = 1.0F;

float ClampUIScale(float scale) {
  if (!std::isfinite(scale)) {
    return 1.0F;
  }

  return std::clamp(scale, MinimumUIScale, MaximumUIScale);
}
} // namespace

float CalculateUIScale(float width, float height) {
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0F
      || height <= 0.0F) {
    return 1.0F;
  }

  const float scaleX = width / ReferenceUIWidth;
  const float scaleY = height / ReferenceUIHeight;
  return ClampUIScale(std::min(scaleX, scaleY));
}

void SetUIScale(float scale) { uiScale = ClampUIScale(scale); }

float GetUIScale() { return uiScale; }

float Ui(float value) { return value * uiScale; }

Vector2 Ui(Vector2 value) { return {Ui(value.X), Ui(value.Y)}; }

Vector2 UiSize(Vector2 value) {
  if (value.X > 0.0F) {
    value.X = Ui(value.X);
  }
  if (value.Y > 0.0F) {
    value.Y = Ui(value.Y);
  }
  return value;
}
} // namespace FlightUI
