#pragma once

#include <imgui.h>

namespace FlightUI {
enum class SemanticColor {
  Success,
  Warning,
  Error,
};

enum class ThemeColor {
  PropertyRowBackground,
  PropertyRowBackgroundAlternate,
  FoldOutSectionBackground,
  FoldOutSectionBackgroundHovered,
  FoldOutSectionBackgroundActive,
  IconButtonSelected,
  IconButtonSelectedHovered,
};

enum class StatusTone {
  Neutral,
  Info,
  Success,
  Warning,
  Error,
};

struct StatusBadgeStyle {
  ImVec4 Background;
  ImVec4 Text;
};

void ApplyDarkEditorTheme();
ImVec4 GetDarkEditorApplicationBackground();
ImVec4 GetDarkEditorSemanticColor(SemanticColor color);
ImVec4 GetThemeColor(ThemeColor color);
StatusBadgeStyle GetStatusBadgeStyle(StatusTone tone);
} // namespace FlightUI
