#include "flightui/controls/StatusBadge.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIRenderHelpers.hpp"
#include "flightui/core/UIScale.hpp"

#include <imgui.h>
#include <utility>

namespace FlightUI {
class StatusBadgeBuilder::Impl {
public:
  std::string Label;
  StatusTone Tone = StatusTone::Neutral;
  std::string Tooltip;
};

StatusBadgeBuilder::StatusBadgeBuilder(std::string label, StatusTone tone)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Label = std::move(label);
  m_Impl->Tone = tone;
}

StatusBadgeBuilder::StatusBadgeBuilder(const StatusBadgeBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

StatusBadgeBuilder::StatusBadgeBuilder(
    StatusBadgeBuilder &&other) noexcept = default;

StatusBadgeBuilder &StatusBadgeBuilder::operator=(
    const StatusBadgeBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }
  return *this;
}

StatusBadgeBuilder &StatusBadgeBuilder::operator=(
    StatusBadgeBuilder &&other) noexcept = default;

StatusBadgeBuilder::~StatusBadgeBuilder() = default;

StatusBadgeBuilder &StatusBadgeBuilder::SetTone(StatusTone tone) {
  m_Impl->Tone = tone;
  return *this;
}

StatusBadgeBuilder &StatusBadgeBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

StatusBadgeBuilder &StatusBadgeBuilder::Tone(StatusTone tone) {
  return SetTone(tone);
}

StatusBadgeBuilder &StatusBadgeBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

StatusBadgeBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  return CreateElement([state] {
    constexpr float HorizontalPadding = 6.0F;
    constexpr float VerticalPadding = 2.0F;
    constexpr float Rounding = 3.0F;
    const StatusBadgeStyle colors = GetStatusBadgeStyle(state.Tone);
    const ImVec2 textSize = ImGui::CalcTextSize(state.Label.c_str());
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImVec2 size{textSize.x + Ui(HorizontalPadding * 2.0F),
        textSize.y + Ui(VerticalPadding * 2.0F)};
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(position,
        ImVec2(position.x + size.x, position.y + size.y),
        ImGui::ColorConvertFloat4ToU32(colors.Background),
        Ui(Rounding));
    drawList->AddText(ImVec2(position.x + Ui(HorizontalPadding),
                          position.y + Ui(VerticalPadding)),
        ImGui::ColorConvertFloat4ToU32(colors.Text),
        state.Label.c_str());
    ImGui::Dummy(size);
    Internal::ShowTooltipIfHovered(state.Tooltip);
  });
}

StatusBadgeBuilder StatusBadge(std::string label, StatusTone tone) {
  return StatusBadgeBuilder(std::move(label), tone);
}
} // namespace FlightUI
