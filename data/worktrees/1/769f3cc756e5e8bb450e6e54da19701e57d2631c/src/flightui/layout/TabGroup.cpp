#include "flightui/layout/TabGroup.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIRenderHelpers.hpp"

#include <imgui.h>

#include <utility>

namespace FlightUI {
class TabBuilder::Impl {
public:
  std::string Label;
  bool *Open = nullptr;
  ImGuiTabItemFlags Flags = ImGuiTabItemFlags_None;
  bool Enabled = true;
  bool Visible = true;
  std::string Tooltip;
  std::string Id;
};

class TabGroupBuilder::Impl {
public:
  std::string Name;
  ImGuiTabBarFlags Flags = ImGuiTabBarFlags_None;
  bool Enabled = true;
  bool Visible = true;
  std::string Tooltip;
  std::string Id;
};

namespace {
std::string MakeTabLabel(const std::string &label, const std::string &id) {
  if (id.empty()) {
    return label;
  }

  return label + "###" + id;
}
} // namespace

TabBuilder::TabBuilder(std::string label) : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Label = std::move(label);
}

TabBuilder::TabBuilder(const TabBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

TabBuilder::TabBuilder(TabBuilder &&other) noexcept = default;

TabBuilder &TabBuilder::operator=(const TabBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

TabBuilder &TabBuilder::operator=(TabBuilder &&other) noexcept = default;

TabBuilder::~TabBuilder() = default;

TabBuilder &TabBuilder::SetOpen(bool &isOpen) {
  m_Impl->Open = &isOpen;
  return *this;
}

TabBuilder &TabBuilder::SetFlags(ImGuiTabItemFlags flags) {
  m_Impl->Flags = flags;
  return *this;
}

TabBuilder &TabBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

TabBuilder &TabBuilder::SetVisible(bool visible) {
  m_Impl->Visible = visible;
  return *this;
}

TabBuilder &TabBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

TabBuilder &TabBuilder::SetId(std::string id) {
  m_Impl->Id = std::move(id);
  return *this;
}

TabBuilder &TabBuilder::Open(bool &isOpen) { return SetOpen(isOpen); }

TabBuilder &TabBuilder::Flags(ImGuiTabItemFlags flags) {
  return SetFlags(flags);
}

TabBuilder &TabBuilder::Enabled(bool enabled) { return SetEnabled(enabled); }

TabBuilder &TabBuilder::Visible(bool visible) { return SetVisible(visible); }

TabBuilder &TabBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

TabBuilder &TabBuilder::Id(std::string id) { return SetId(std::move(id)); }

UIElement TabBuilder::operator[](UIElement child) const {
  Children children;
  children.push_back(std::move(child));
  return (*this)[std::move(children)];
}

UIElement TabBuilder::operator[](ChildrenBuilder children) const {
  return (*this)[std::move(children).TakeChildren()];
}

UIElement TabBuilder::operator[](Children children) const {
  Impl state = *m_Impl;
  return CreateElement([state, children = std::move(children)] {
    if (!state.Visible) {
      return;
    }

    bool localOpen = state.Open == nullptr || *state.Open;
    if (!localOpen) {
      return;
    }

    const std::string label = MakeTabLabel(state.Label, state.Id);
    Internal::DisabledScope disabledScope(!state.Enabled);
    const bool isSelected = ImGui::BeginTabItem(
        label.c_str(), state.Open == nullptr ? nullptr : &localOpen,
        state.Flags);

    Internal::ShowTooltipIfHovered(state.Tooltip);

    if (isSelected) {
      for (const UIElement &childElement : children) {
        childElement.Render();
      }

      ImGui::EndTabItem();
    }

    if (state.Open != nullptr) {
      *state.Open = localOpen;
    }
  });
}

TabGroupBuilder::TabGroupBuilder(std::string name)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Name = std::move(name);
}

TabGroupBuilder::TabGroupBuilder(const TabGroupBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

TabGroupBuilder::TabGroupBuilder(TabGroupBuilder &&other) noexcept = default;

TabGroupBuilder &TabGroupBuilder::operator=(const TabGroupBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

TabGroupBuilder &
TabGroupBuilder::operator=(TabGroupBuilder &&other) noexcept = default;

TabGroupBuilder::~TabGroupBuilder() = default;

TabGroupBuilder &TabGroupBuilder::SetFlags(ImGuiTabBarFlags flags) {
  m_Impl->Flags = flags;
  return *this;
}

TabGroupBuilder &TabGroupBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

TabGroupBuilder &TabGroupBuilder::SetVisible(bool visible) {
  m_Impl->Visible = visible;
  return *this;
}

TabGroupBuilder &TabGroupBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

TabGroupBuilder &TabGroupBuilder::SetId(std::string id) {
  m_Impl->Id = std::move(id);
  return *this;
}

TabGroupBuilder &TabGroupBuilder::Flags(ImGuiTabBarFlags flags) {
  return SetFlags(flags);
}

TabGroupBuilder &TabGroupBuilder::Enabled(bool enabled) {
  return SetEnabled(enabled);
}

TabGroupBuilder &TabGroupBuilder::Visible(bool visible) {
  return SetVisible(visible);
}

TabGroupBuilder &TabGroupBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

TabGroupBuilder &TabGroupBuilder::Id(std::string id) {
  return SetId(std::move(id));
}

UIElement TabGroupBuilder::operator[](UIElement child) const {
  Children children;
  children.push_back(std::move(child));
  return (*this)[std::move(children)];
}

UIElement TabGroupBuilder::operator[](ChildrenBuilder children) const {
  return (*this)[std::move(children).TakeChildren()];
}

UIElement TabGroupBuilder::operator[](Children children) const {
  Impl state = *m_Impl;
  return CreateElement([state, children = std::move(children)] {
    if (!state.Visible) {
      return;
    }

    const std::string id = state.Id.empty() ? state.Name : state.Id;
    Internal::DisabledScope disabledScope(!state.Enabled);

    if (ImGui::BeginTabBar(id.c_str(), state.Flags)) {
      Internal::ShowTooltipIfHovered(state.Tooltip);

      for (const UIElement &childElement : children) {
        childElement.Render();
      }

      ImGui::EndTabBar();
    }
  });
}

TabBuilder Tab(std::string label) { return TabBuilder(std::move(label)); }

TabGroupBuilder TabGroup(std::string name) {
  return TabGroupBuilder(std::move(name));
}
} // namespace FlightUI
