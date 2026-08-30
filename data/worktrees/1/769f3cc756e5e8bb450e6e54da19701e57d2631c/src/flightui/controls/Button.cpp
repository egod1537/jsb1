#include "flightui/controls/Button.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIRenderHelpers.hpp"
#include "flightui/core/UIScale.hpp"

#include <imgui.h>

#include <utility>

namespace FlightUI {
class ButtonBuilder::Impl {
public:
  std::string Label;
  Action OnClick;
  Vector2 Size;
  bool Enabled = true;
  std::string Tooltip;
  std::string Id;
};

namespace {
ImVec2 ToImVec2(Vector2 value) { return ImVec2(value.X, value.Y); }
} // namespace

ButtonBuilder::ButtonBuilder(std::string label, Action onClick)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Label = std::move(label);
  m_Impl->OnClick = std::move(onClick);
}

ButtonBuilder::ButtonBuilder(const ButtonBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

ButtonBuilder::ButtonBuilder(ButtonBuilder &&other) noexcept = default;

ButtonBuilder &ButtonBuilder::operator=(const ButtonBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

ButtonBuilder &ButtonBuilder::operator=(
    ButtonBuilder &&other) noexcept = default;

ButtonBuilder::~ButtonBuilder() = default;

ButtonBuilder &ButtonBuilder::SetOnAction(Action onClick) {
  m_Impl->OnClick = std::move(onClick);
  return *this;
}

ButtonBuilder &ButtonBuilder::SetSize(Vector2 size) {
  m_Impl->Size = size;
  return *this;
}

ButtonBuilder &ButtonBuilder::SetWidth(float width) {
  m_Impl->Size.X = width;
  return *this;
}

ButtonBuilder &ButtonBuilder::SetHeight(float height) {
  m_Impl->Size.Y = height;
  return *this;
}

ButtonBuilder &ButtonBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

ButtonBuilder &ButtonBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

ButtonBuilder &ButtonBuilder::SetId(std::string id) {
  m_Impl->Id = std::move(id);
  return *this;
}

ButtonBuilder &ButtonBuilder::OnAction(Action onClick) {
  return SetOnAction(std::move(onClick));
}

ButtonBuilder &ButtonBuilder::Size(Vector2 size) { return SetSize(size); }

ButtonBuilder &ButtonBuilder::Width(float width) { return SetWidth(width); }

ButtonBuilder &ButtonBuilder::Widht(float width) { return SetWidth(width); }

ButtonBuilder &ButtonBuilder::Height(float height) { return SetHeight(height); }

ButtonBuilder &ButtonBuilder::Enabled(bool enabled) {
  return SetEnabled(enabled);
}

ButtonBuilder &ButtonBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

ButtonBuilder &ButtonBuilder::Id(std::string id) {
  return SetId(std::move(id));
}

ButtonBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  return CreateElement([state] {
    Internal::IdScope idScope(state.Id);
    Internal::DisabledScope disabledScope(!state.Enabled);

    if (ImGui::Button(state.Label.c_str(), ToImVec2(UiSize(state.Size)))
        && state.OnClick) {
      state.OnClick();
    }

    Internal::ShowTooltipIfHovered(state.Tooltip);
  });
}

ButtonBuilder Button(std::string label, Action onClick) {
  return ButtonBuilder(std::move(label), std::move(onClick));
}
} // namespace FlightUI
