#include "flightui/controls/Combo.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIRenderHelpers.hpp"

#include <imgui.h>

#include <utility>

namespace FlightUI {
class ComboBuilder::Impl {
public:
  std::string Label;
  int SelectedIndex = 0;
  std::vector<std::string> Items;
  ComboChangedAction OnChanged;
  float Width = 0.0F;
  bool Enabled = true;
  std::string Tooltip;
  std::string Id;
};

ComboBuilder::ComboBuilder(std::string label, int selectedIndex,
    std::vector<std::string> items)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Label = std::move(label);
  m_Impl->SelectedIndex = selectedIndex;
  m_Impl->Items = std::move(items);
}

ComboBuilder::ComboBuilder(const ComboBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

ComboBuilder::ComboBuilder(ComboBuilder &&other) noexcept = default;

ComboBuilder &ComboBuilder::operator=(const ComboBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

ComboBuilder &ComboBuilder::operator=(ComboBuilder &&other) noexcept = default;

ComboBuilder::~ComboBuilder() = default;

ComboBuilder &ComboBuilder::SetOnChanged(ComboChangedAction onChanged) {
  m_Impl->OnChanged = std::move(onChanged);
  return *this;
}

ComboBuilder &ComboBuilder::SetWidth(float width) {
  m_Impl->Width = width;
  return *this;
}

ComboBuilder &ComboBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

ComboBuilder &ComboBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

ComboBuilder &ComboBuilder::SetId(std::string id) {
  m_Impl->Id = std::move(id);
  return *this;
}

ComboBuilder &ComboBuilder::OnChanged(ComboChangedAction onChanged) {
  return SetOnChanged(std::move(onChanged));
}

ComboBuilder &ComboBuilder::Width(float width) { return SetWidth(width); }

ComboBuilder &ComboBuilder::Enabled(bool enabled) {
  return SetEnabled(enabled);
}

ComboBuilder &ComboBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

ComboBuilder &ComboBuilder::Id(std::string id) { return SetId(std::move(id)); }

ComboBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  return CreateElement([state] {
    Internal::IdScope idScope(state.Id);
    Internal::DisabledScope disabledScope(!state.Enabled);
    Internal::ItemWidthScope widthScope(state.Width);

    const bool hasSelectedItem =
        state.SelectedIndex >= 0
        && state.SelectedIndex < static_cast<int>(state.Items.size());
    const char *preview =
        hasSelectedItem ? state.Items[state.SelectedIndex].c_str() : "";

    if (ImGui::BeginCombo(state.Label.c_str(), preview)) {
      for (int index = 0; index < static_cast<int>(state.Items.size());
          ++index) {
        const bool selected = index == state.SelectedIndex;
        if (ImGui::Selectable(state.Items[index].c_str(), selected)
            && state.OnChanged) {
          state.OnChanged(index);
        }

        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    Internal::ShowTooltipIfHovered(state.Tooltip);
  });
}

ComboBuilder Combo(std::string label, int selectedIndex,
    std::vector<std::string> items) {
  return ComboBuilder(std::move(label), selectedIndex, std::move(items));
}
} // namespace FlightUI
