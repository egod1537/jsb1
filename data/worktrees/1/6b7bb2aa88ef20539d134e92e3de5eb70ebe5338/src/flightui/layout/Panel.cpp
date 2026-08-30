#include "flightui/layout/Panel.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIRenderHelpers.hpp"
#include "flightui/core/UIScale.hpp"

#include <imgui.h>

#include <utility>

namespace FlightUI {
class PanelBuilder::Impl {
public:
  std::string Name;
  Vector2 Size;
  bool FlexibleWidth = false;
  bool FlexibleHeight = false;
  bool Border = false;
  ImGuiChildFlags Flags = ImGuiChildFlags_None;
  bool Enabled = true;
  bool Visible = true;
  std::string Tooltip;
  std::string Id;
};

namespace {
ImVec2 ToImVec2(Vector2 value) { return ImVec2(value.X, value.Y); }
} // namespace

PanelBuilder::PanelBuilder(std::string name)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Name = std::move(name);
}

PanelBuilder::PanelBuilder(const PanelBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

PanelBuilder::PanelBuilder(PanelBuilder &&other) noexcept = default;

PanelBuilder &PanelBuilder::operator=(const PanelBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

PanelBuilder &PanelBuilder::operator=(PanelBuilder &&other) noexcept = default;

PanelBuilder::~PanelBuilder() = default;

PanelBuilder &PanelBuilder::SetWidth(float width) {
  m_Impl->Size.X = width;
  m_Impl->FlexibleWidth = false;
  return *this;
}

PanelBuilder &PanelBuilder::SetHeight(float height) {
  m_Impl->Size.Y = height;
  m_Impl->FlexibleHeight = false;
  return *this;
}

PanelBuilder &PanelBuilder::SetSize(Vector2 size) {
  m_Impl->Size = size;
  m_Impl->FlexibleWidth = false;
  m_Impl->FlexibleHeight = false;
  return *this;
}

PanelBuilder &PanelBuilder::SetFlexibleWidth(bool flexible) {
  m_Impl->FlexibleWidth = flexible;
  return *this;
}

PanelBuilder &PanelBuilder::SetFlexibleHeight(bool flexible) {
  m_Impl->FlexibleHeight = flexible;
  return *this;
}

PanelBuilder &PanelBuilder::SetBorder(bool enabled) {
  m_Impl->Border = enabled;
  return *this;
}

PanelBuilder &PanelBuilder::SetFlags(ImGuiChildFlags flags) {
  m_Impl->Flags = flags;
  return *this;
}

PanelBuilder &PanelBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

PanelBuilder &PanelBuilder::SetVisible(bool visible) {
  m_Impl->Visible = visible;
  return *this;
}

PanelBuilder &PanelBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

PanelBuilder &PanelBuilder::SetId(std::string id) {
  m_Impl->Id = std::move(id);
  return *this;
}

PanelBuilder &PanelBuilder::Width(float width) { return SetWidth(width); }

PanelBuilder &PanelBuilder::Height(float height) { return SetHeight(height); }

PanelBuilder &PanelBuilder::Size(Vector2 size) { return SetSize(size); }

PanelBuilder &PanelBuilder::FlexibleWidth(bool flexible) {
  return SetFlexibleWidth(flexible);
}

PanelBuilder &PanelBuilder::FlexibleHeight(bool flexible) {
  return SetFlexibleHeight(flexible);
}

PanelBuilder &PanelBuilder::Border(bool enabled) { return SetBorder(enabled); }

PanelBuilder &PanelBuilder::Flags(ImGuiChildFlags flags) {
  return SetFlags(flags);
}

PanelBuilder &PanelBuilder::Enabled(bool enabled) {
  return SetEnabled(enabled);
}

PanelBuilder &PanelBuilder::Visible(bool visible) {
  return SetVisible(visible);
}

PanelBuilder &PanelBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

PanelBuilder &PanelBuilder::Id(std::string id) { return SetId(std::move(id)); }

UIElement PanelBuilder::operator[](UIElement child) const {
  Children children;
  children.push_back(std::move(child));
  return (*this)[std::move(children)];
}

UIElement PanelBuilder::operator[](ChildrenBuilder children) const {
  return (*this)[std::move(children).TakeChildren()];
}

UIElement PanelBuilder::operator[](Children children) const {
  Impl state = *m_Impl;
  return CreateElement([state, children = std::move(children)] {
    if (!state.Visible) {
      return;
    }

    ImGuiChildFlags flags = state.Flags;
    if (state.Border) {
      flags |= ImGuiChildFlags_Borders;
    }

    Vector2 size = state.Size;
    if (state.FlexibleWidth) {
      size.X = 0.0F;
    }
    if (state.FlexibleHeight) {
      size.Y = 0.0F;
    }
    size = UiSize(size);

    const std::string childId = state.Id.empty() ? state.Name : state.Id;
    Internal::DisabledScope disabledScope(!state.Enabled);
    const bool isVisible =
        ImGui::BeginChild(childId.c_str(), ToImVec2(size), flags);

    if (!state.Tooltip.empty()
        && ImGui::IsWindowHovered(ImGuiHoveredFlags_DelayNormal)) {
      ImGui::SetTooltip("%s", state.Tooltip.c_str());
    }

    if (isVisible) {
      for (const UIElement &childElement : children) {
        childElement.Render();
      }
    }

    ImGui::EndChild();
  });
}

PanelBuilder Panel(std::string name) { return PanelBuilder(std::move(name)); }
} // namespace FlightUI
