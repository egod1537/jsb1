#include "flightui/core/Theme.hpp"

#include <cstdint>
#include <imgui.h>
#include <implot.h>

namespace FlightUI {
namespace {
ImVec4 Color(std::uint32_t rgb, float alpha = 1.0F) {
  constexpr float ByteToFloat = 1.0F / 255.0F;
  return {
      static_cast<float>((rgb >> 16U) & 0xFFU) * ByteToFloat,
      static_cast<float>((rgb >> 8U) & 0xFFU) * ByteToFloat,
      static_cast<float>(rgb & 0xFFU) * ByteToFloat,
      alpha,
  };
}

struct DarkEditorPalette {
  ImVec4 applicationBackground = Color(0x181A1F);
  ImVec4 windowBackground = Color(0x1E2127);
  ImVec4 childBackground = Color(0x20232A);
  ImVec4 popupBackground = Color(0x24272E, 0.99F);
  ImVec4 frameBackground = Color(0x292D35);
  ImVec4 frameHovered = Color(0x333A46);
  ImVec4 frameActive = Color(0x384454);
  ImVec4 border = Color(0x373C46);
  ImVec4 separator = Color(0x343943);
  ImVec4 text = Color(0xD8DCE3);
  ImVec4 textDisabled = Color(0x747B87);
  ImVec4 accent = Color(0x4C8DFF);
  ImVec4 accentHovered = Color(0x65A0FF);
  ImVec4 accentActive = Color(0x3678E5);
  ImVec4 success = Color(0x63A177);
  ImVec4 warning = Color(0xC49354);
  ImVec4 error = Color(0xC76969);
  ImVec4 propertyRowBackground = Color(0x181A1F, 0.0F);
  ImVec4 propertyRowBackgroundAlternate = Color(0x30353E, 0.22F);
  ImVec4 foldOutSectionBackground = Color(0x292D35, 0.72F);
  ImVec4 foldOutSectionBackgroundHovered = Color(0x333A46, 0.82F);
  ImVec4 foldOutSectionBackgroundActive = Color(0x384454, 0.88F);
  ImVec4 iconButtonSelected = Color(0x315B8E);
  ImVec4 iconButtonSelectedHovered = Color(0x3A6BA6);
};

const DarkEditorPalette &Palette() {
  static const DarkEditorPalette palette;
  return palette;
}

void ApplyImGuiTheme(const DarkEditorPalette &palette) {
  ImGuiStyle &style = ImGui::GetStyle();
  ImGui::StyleColorsDark(&style);

  style.WindowPadding = ImVec2(10.0F, 10.0F);
  style.FramePadding = ImVec2(8.0F, 5.0F);
  style.ItemSpacing = ImVec2(8.0F, 6.0F);
  style.ItemInnerSpacing = ImVec2(6.0F, 4.0F);
  style.CellPadding = ImVec2(6.0F, 4.0F);
  style.IndentSpacing = 21.0F;
  style.ScrollbarSize = 12.0F;
  style.GrabMinSize = 10.0F;

  style.WindowRounding = 5.0F;
  style.ChildRounding = 4.0F;
  style.PopupRounding = 4.0F;
  style.FrameRounding = 4.0F;
  style.ScrollbarRounding = 4.0F;
  style.GrabRounding = 4.0F;
  style.TabRounding = 4.0F;
  style.MenuItemRounding = 3.0F;
  style.SelectableRounding = 3.0F;

  style.WindowBorderSize = 1.0F;
  style.ChildBorderSize = 1.0F;
  style.PopupBorderSize = 1.0F;
  style.FrameBorderSize = 0.0F;
  style.TabBorderSize = 0.0F;
  style.TabBarBorderSize = 1.0F;
  style.TabBarOverlineSize = 2.0F;
  style.SeparatorSize = 1.0F;
  style.DockingSeparatorSize = 1.0F;

  ImVec4 *colors = style.Colors;
  colors[ImGuiCol_Text] = palette.text;
  colors[ImGuiCol_TextDisabled] = palette.textDisabled;
  colors[ImGuiCol_WindowBg] = palette.windowBackground;
  colors[ImGuiCol_ChildBg] = palette.childBackground;
  colors[ImGuiCol_PopupBg] = palette.popupBackground;
  colors[ImGuiCol_Border] = palette.border;
  colors[ImGuiCol_BorderShadow] = Color(0x111318, 0.22F);
  colors[ImGuiCol_FrameBg] = palette.frameBackground;
  colors[ImGuiCol_FrameBgHovered] = palette.frameHovered;
  colors[ImGuiCol_FrameBgActive] = palette.frameActive;
  colors[ImGuiCol_TitleBg] = Color(0x1B1E24);
  colors[ImGuiCol_TitleBgActive] = Color(0x22262E);
  colors[ImGuiCol_TitleBgCollapsed] = Color(0x1B1E24);
  colors[ImGuiCol_MenuBarBg] = Color(0x1B1E24);
  colors[ImGuiCol_ScrollbarBg] = Color(0x1A1D23, 0.72F);
  colors[ImGuiCol_ScrollbarGrab] = Color(0x444A55);
  colors[ImGuiCol_ScrollbarGrabHovered] = Color(0x586171);
  colors[ImGuiCol_ScrollbarGrabActive] = Color(0x667185);
  colors[ImGuiCol_CheckMark] = palette.accentHovered;
  colors[ImGuiCol_CheckboxSelectedBg] = Color(0x315F9F);
  colors[ImGuiCol_SliderGrab] = palette.accent;
  colors[ImGuiCol_SliderGrabActive] = palette.accentHovered;
  colors[ImGuiCol_Button] = palette.frameBackground;
  colors[ImGuiCol_ButtonHovered] = palette.frameHovered;
  colors[ImGuiCol_ButtonActive] = Color(0x305B94);
  colors[ImGuiCol_Header] = Color(0x2C3440);
  colors[ImGuiCol_HeaderHovered] = Color(0x34475F);
  colors[ImGuiCol_HeaderActive] = Color(0x315B8E);
  colors[ImGuiCol_Separator] = palette.separator;
  colors[ImGuiCol_SeparatorHovered] = Color(0x4274B6);
  colors[ImGuiCol_SeparatorActive] = palette.accent;
  colors[ImGuiCol_ResizeGrip] = Color(0x4C8DFF, 0.18F);
  colors[ImGuiCol_ResizeGripHovered] = Color(0x65A0FF, 0.58F);
  colors[ImGuiCol_ResizeGripActive] = palette.accent;
  colors[ImGuiCol_InputTextCursor] = palette.text;
  colors[ImGuiCol_TabHovered] = Color(0x303A49);
  colors[ImGuiCol_Tab] = Color(0x1B1E24);
  colors[ImGuiCol_TabSelected] = Color(0x292E37);
  colors[ImGuiCol_TabSelectedOverline] = palette.accent;
  colors[ImGuiCol_TabDimmed] = Color(0x191C21);
  colors[ImGuiCol_TabDimmedSelected] = Color(0x242831);
  colors[ImGuiCol_TabDimmedSelectedOverline] = Color(0x3D70B8);
  colors[ImGuiCol_DockingPreview] = Color(0x4C8DFF, 0.48F);
  colors[ImGuiCol_DockingEmptyBg] = palette.applicationBackground;
  colors[ImGuiCol_PlotLines] = palette.accentHovered;
  colors[ImGuiCol_PlotLinesHovered] = Color(0x86B6FF);
  colors[ImGuiCol_PlotHistogram] = palette.warning;
  colors[ImGuiCol_PlotHistogramHovered] = Color(0xD6A66A);
  colors[ImGuiCol_TableHeaderBg] = Color(0x262A32);
  colors[ImGuiCol_TableBorderStrong] = palette.border;
  colors[ImGuiCol_TableBorderLight] = Color(0x30353E);
  colors[ImGuiCol_TableRowBg] = palette.propertyRowBackground;
  colors[ImGuiCol_TableRowBgAlt] = palette.propertyRowBackgroundAlternate;
  colors[ImGuiCol_TextLink] = palette.accentHovered;
  colors[ImGuiCol_TextSelectedBg] = Color(0x4C8DFF, 0.38F);
  colors[ImGuiCol_TreeLines] = Color(0x464D59);
  colors[ImGuiCol_DragDropTarget] = palette.accentHovered;
  colors[ImGuiCol_DragDropTargetBg] = Color(0x4C8DFF, 0.16F);
  colors[ImGuiCol_UnsavedMarker] = palette.warning;
  colors[ImGuiCol_NavCursor] = palette.accentHovered;
  colors[ImGuiCol_NavWindowingHighlight] = Color(0xD8DCE3, 0.62F);
  colors[ImGuiCol_NavWindowingDimBg] = Color(0x14161B, 0.54F);
  colors[ImGuiCol_ModalWindowDimBg] = Color(0x14161B, 0.66F);
}

void ApplyImPlotTheme(const DarkEditorPalette &palette) {
  ImPlotStyle &style = ImPlot::GetStyle();
  ImPlot::StyleColorsDark(&style);

  style.PlotBorderSize = 1.0F;
  style.MinorAlpha = 0.30F;
  style.Colormap = ImPlotColormap_Deep;

  ImVec4 *colors = style.Colors;
  colors[ImPlotCol_FrameBg] = palette.childBackground;
  colors[ImPlotCol_PlotBg] = palette.applicationBackground;
  colors[ImPlotCol_PlotBorder] = palette.border;
  colors[ImPlotCol_LegendBg] = Color(0x1E2127, 0.96F);
  colors[ImPlotCol_LegendBorder] = palette.border;
  colors[ImPlotCol_LegendText] = palette.text;
  colors[ImPlotCol_TitleText] = palette.text;
  colors[ImPlotCol_InlayText] = palette.text;
  colors[ImPlotCol_AxisText] = Color(0xAAB0BB);
  colors[ImPlotCol_AxisGrid] = Color(0x7A8494, 0.20F);
  colors[ImPlotCol_AxisTick] = Color(0x8B94A3, 0.46F);
  colors[ImPlotCol_AxisBg] = Color(0x181A1F, 0.0F);
  colors[ImPlotCol_AxisBgHovered] = Color(0x333A46, 0.68F);
  colors[ImPlotCol_AxisBgActive] = Color(0x4C8DFF, 0.30F);
  colors[ImPlotCol_Selection] = palette.accentHovered;
  colors[ImPlotCol_Crosshairs] = Color(0xAAB0BB, 0.72F);
}
} // namespace

void ApplyDarkEditorTheme() {
  const DarkEditorPalette &palette = Palette();
  ApplyImGuiTheme(palette);
  ApplyImPlotTheme(palette);
}

ImVec4 GetDarkEditorApplicationBackground() {
  return Palette().applicationBackground;
}

ImVec4 GetDarkEditorSemanticColor(SemanticColor color) {
  const DarkEditorPalette &palette = Palette();
  switch (color) {
  case SemanticColor::Success:
    return palette.success;
  case SemanticColor::Warning:
    return palette.warning;
  case SemanticColor::Error:
    return palette.error;
  }

  return palette.textDisabled;
}

ImVec4 GetThemeColor(ThemeColor color) {
  const DarkEditorPalette &palette = Palette();
  switch (color) {
  case ThemeColor::PropertyRowBackground:
    return palette.propertyRowBackground;
  case ThemeColor::PropertyRowBackgroundAlternate:
    return palette.propertyRowBackgroundAlternate;
  case ThemeColor::FoldOutSectionBackground:
    return palette.foldOutSectionBackground;
  case ThemeColor::FoldOutSectionBackgroundHovered:
    return palette.foldOutSectionBackgroundHovered;
  case ThemeColor::FoldOutSectionBackgroundActive:
    return palette.foldOutSectionBackgroundActive;
  case ThemeColor::IconButtonSelected:
    return palette.iconButtonSelected;
  case ThemeColor::IconButtonSelectedHovered:
    return palette.iconButtonSelectedHovered;
  }

  return palette.propertyRowBackground;
}

StatusBadgeStyle GetStatusBadgeStyle(StatusTone tone) {
  const DarkEditorPalette &palette = Palette();
  switch (tone) {
  case StatusTone::Neutral:
    return {Color(0x747B87, 0.16F), Color(0xAAB0BB)};
  case StatusTone::Info:
    return {Color(0x4C8DFF, 0.18F), palette.accentHovered};
  case StatusTone::Success:
    return {Color(0x63A177, 0.18F), Color(0x83BB91)};
  case StatusTone::Warning:
    return {Color(0xC49354, 0.18F), Color(0xD6A66A)};
  case StatusTone::Error:
    return {Color(0xC76969, 0.18F), Color(0xDB8585)};
  }

  return {Color(0x747B87, 0.16F), palette.textDisabled};
}
} // namespace FlightUI
