#pragma once

#include "flightui/core/UICommon.hpp"

namespace FlightUI {
inline constexpr float ReferenceUIWidth = 1280.0F;
inline constexpr float ReferenceUIHeight = 720.0F;
inline constexpr float MinimumUIScale = 0.70F;
inline constexpr float MaximumUIScale = 1.50F;

float CalculateUIScale(float width, float height);
void SetUIScale(float scale);
float GetUIScale();

float Ui(float value);
Vector2 Ui(Vector2 value);
Vector2 UiSize(Vector2 value);
} // namespace FlightUI
