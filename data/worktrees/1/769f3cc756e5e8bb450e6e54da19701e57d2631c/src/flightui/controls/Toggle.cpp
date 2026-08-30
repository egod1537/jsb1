#include "flightui/controls/Toggle.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIRenderHelpers.hpp"

#include <imgui.h>

#include <utility>

namespace FlightUI {
class ToggleBuilder::Impl {
public:
  std::string Label;
  bool Value = false;
  ToggleChangedAction OnChanged;
  bool Enabled = true;
  std::string Tooltip;
  std::string Id;
};

ToggleBuilder::ToggleBuilder(std::string label, bool value)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Label = std::move(label);
  m_Impl->Value = value;
}

ToggleBuilder::ToggleBuilder(const ToggleBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

ToggleBuilder::ToggleBuilder(ToggleBuilder &&other) noexcept = default;

ToggleBuilder &ToggleBuilder::operator=(const ToggleBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

ToggleBuilder &
ToggleBuilder::operator=(ToggleBuilder &&other) noexcept = default;

ToggleBuilder::~ToggleBuilder() = default;

ToggleBuilder &ToggleBuilder::SetOnChanged(ToggleChangedAction onChanged) {
  m_Impl->OnChanged = std::move(onChanged);
  return *this;
}

ToggleBuilder &ToggleBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

ToggleBuilder &ToggleBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

ToggleBuilder &ToggleBuilder::SetId(std::string id) {
  m_Impl->Id = std::move(id);
  return *this;
}

ToggleBuilder &ToggleBuilder::OnChanged(ToggleChangedAction onChanged) {
  return SetOnChanged(std::move(onChanged));
}

ToggleBuilder &ToggleBuilder::Enabled(bool enabled) {
  return SetEnabled(enabled);
}

ToggleBuilder &ToggleBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

ToggleBuilder &ToggleBuilder::Id(std::string id) {
  return SetId(std::move(id));
}

ToggleBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  return CreateElement([state] {
    Internal::IdScope idScope(state.Id);
    Internal::DisabledScope disabledScope(!state.Enabled);
    bool value = state.Value;
    if (ImGui::Checkbox(state.Label.c_str(), &value) && state.OnChanged) {
      state.OnChanged(value);
    }
    Internal::ShowTooltipIfHovered(state.Tooltip);
  });
}

ToggleBuilder Toggle(std::string label, bool value) {
  return ToggleBuilder(std::move(label), value);
}
} // namespace FlightUI
