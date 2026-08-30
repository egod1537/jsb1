#include "flightui/controls/IconButton.hpp"

#include "flightui/core/Theme.hpp"
#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIRenderHelpers.hpp"
#include "flightui/core/UIScale.hpp"

#include <imgui.h>
#include <utility>

namespace FlightUI {
class IconButtonBuilder::Impl {
public:
  std::string Id;
  ImTextureID Texture = ImTextureID_Invalid;
  std::string FallbackText = "?";
  float Size = 24.0F;
  bool Selected = false;
  bool Toggle = false;
  bool Enabled = true;
  std::string Tooltip;
  IconButtonAction OnAction;
  ToggleIconButtonChangedAction OnChanged;
};

IconButtonBuilder::IconButtonBuilder(std::string id, ImTextureID texture)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Id = std::move(id);
  m_Impl->Texture = texture;
}

IconButtonBuilder::IconButtonBuilder(const IconButtonBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

IconButtonBuilder::IconButtonBuilder(
    IconButtonBuilder &&other) noexcept = default;

IconButtonBuilder &IconButtonBuilder::operator=(
    const IconButtonBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }
  return *this;
}

IconButtonBuilder &IconButtonBuilder::operator=(
    IconButtonBuilder &&other) noexcept = default;

IconButtonBuilder::~IconButtonBuilder() = default;

IconButtonBuilder &IconButtonBuilder::SetFallbackText(std::string text) {
  m_Impl->FallbackText = std::move(text);
  return *this;
}

IconButtonBuilder &IconButtonBuilder::SetSize(float size) {
  m_Impl->Size = size;
  return *this;
}

IconButtonBuilder &IconButtonBuilder::SetSelected(bool selected) {
  m_Impl->Selected = selected;
  return *this;
}

IconButtonBuilder &IconButtonBuilder::SetToggle(bool toggle) {
  m_Impl->Toggle = toggle;
  return *this;
}

IconButtonBuilder &IconButtonBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

IconButtonBuilder &IconButtonBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

IconButtonBuilder &IconButtonBuilder::SetOnAction(IconButtonAction onAction) {
  m_Impl->OnAction = std::move(onAction);
  return *this;
}

IconButtonBuilder &IconButtonBuilder::SetOnChanged(
    ToggleIconButtonChangedAction onChanged) {
  m_Impl->OnChanged = std::move(onChanged);
  return *this;
}

IconButtonBuilder &IconButtonBuilder::FallbackText(std::string text) {
  return SetFallbackText(std::move(text));
}

IconButtonBuilder &IconButtonBuilder::Size(float size) { return SetSize(size); }

IconButtonBuilder &IconButtonBuilder::Selected(bool selected) {
  return SetSelected(selected);
}

IconButtonBuilder &IconButtonBuilder::Toggle(bool toggle) {
  return SetToggle(toggle);
}

IconButtonBuilder &IconButtonBuilder::Enabled(bool enabled) {
  return SetEnabled(enabled);
}

IconButtonBuilder &IconButtonBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

IconButtonBuilder &IconButtonBuilder::OnAction(IconButtonAction onAction) {
  return SetOnAction(std::move(onAction));
}

IconButtonBuilder &IconButtonBuilder::OnChanged(
    ToggleIconButtonChangedAction onChanged) {
  return SetOnChanged(std::move(onChanged));
}

IconButtonBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  return CreateElement([state] {
    Internal::DisabledScope disabledScope(!state.Enabled);
    if (state.Selected) {
      ImGui::PushStyleColor(ImGuiCol_Button,
          GetThemeColor(ThemeColor::IconButtonSelected));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
          GetThemeColor(ThemeColor::IconButtonSelectedHovered));
    }

    const float extent = Ui(state.Size);
    const ImVec2 size{extent, extent};
    const bool hasTexture = state.Texture != ImTextureID_Invalid;
    const std::string buttonLabel =
        (hasTexture ? std::string() : state.FallbackText) + "###" + state.Id;
    const bool pressed = ImGui::Button(buttonLabel.c_str(), size);
    if (hasTexture) {
      const float padding = Ui(3.0F);
      const ImVec2 minimum = ImGui::GetItemRectMin();
      ImGui::GetWindowDrawList()->AddImage(ImTextureRef(state.Texture),
          ImVec2(minimum.x + padding, minimum.y + padding),
          ImVec2(minimum.x + extent - padding, minimum.y + extent - padding),
          ImVec2(0.0F, 0.0F),
          ImVec2(1.0F, 1.0F),
          ImGui::GetColorU32(
              state.Enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    }
    if (state.Selected) {
      ImGui::PopStyleColor(2);
    }
    if (pressed && state.OnAction) {
      state.OnAction();
    }
    if (pressed && state.Toggle && state.OnChanged) {
      state.OnChanged(!state.Selected);
    }
    Internal::ShowTooltipIfHovered(state.Tooltip);
  });
}

IconButtonBuilder IconButton(std::string id, ImTextureID texture) {
  return IconButtonBuilder(std::move(id), texture);
}

IconButtonBuilder ToggleIconButton(std::string id, ImTextureID texture,
    bool selected) {
  return IconButtonBuilder(std::move(id), texture).Selected(selected).Toggle();
}
} // namespace FlightUI
