#pragma once

#include "flightui/core/UIElement.hpp"

#include <imgui.h>

#include <memory>
#include <string>

namespace FlightUI {
class TabBuilder {
public:
  // Lifetime
  explicit TabBuilder(std::string label);
  TabBuilder(const TabBuilder &other);
  TabBuilder(TabBuilder &&other) noexcept;
  TabBuilder &operator=(const TabBuilder &other);
  TabBuilder &operator=(TabBuilder &&other) noexcept;
  ~TabBuilder();

  // Explicit configuration
  TabBuilder &SetOpen(bool &isOpen);
  TabBuilder &SetFlags(ImGuiTabItemFlags flags);
  TabBuilder &SetEnabled(bool enabled);
  TabBuilder &SetVisible(bool visible);
  TabBuilder &SetTooltip(std::string tooltip);
  TabBuilder &SetId(std::string id);

  // Fluent configuration
  TabBuilder &Open(bool &isOpen);
  TabBuilder &Flags(ImGuiTabItemFlags flags);
  TabBuilder &Enabled(bool enabled);
  TabBuilder &Visible(bool visible);
  TabBuilder &Tooltip(std::string tooltip);
  TabBuilder &Id(std::string id);

  // Child content
  UIElement operator[](UIElement child) const;
  UIElement operator[](Children children) const;
  UIElement operator[](ChildrenBuilder children) const;

private:
  // Implementation state
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};

class TabGroupBuilder {
public:
  // Lifetime
  explicit TabGroupBuilder(std::string name);
  TabGroupBuilder(const TabGroupBuilder &other);
  TabGroupBuilder(TabGroupBuilder &&other) noexcept;
  TabGroupBuilder &operator=(const TabGroupBuilder &other);
  TabGroupBuilder &operator=(TabGroupBuilder &&other) noexcept;
  ~TabGroupBuilder();

  // Explicit configuration
  TabGroupBuilder &SetFlags(ImGuiTabBarFlags flags);
  TabGroupBuilder &SetEnabled(bool enabled);
  TabGroupBuilder &SetVisible(bool visible);
  TabGroupBuilder &SetTooltip(std::string tooltip);
  TabGroupBuilder &SetId(std::string id);

  // Fluent configuration
  TabGroupBuilder &Flags(ImGuiTabBarFlags flags);
  TabGroupBuilder &Enabled(bool enabled);
  TabGroupBuilder &Visible(bool visible);
  TabGroupBuilder &Tooltip(std::string tooltip);
  TabGroupBuilder &Id(std::string id);

  // Child content
  UIElement operator[](UIElement child) const;
  UIElement operator[](Children children) const;
  UIElement operator[](ChildrenBuilder children) const;

private:
  // Implementation state
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};

TabBuilder Tab(std::string label);
TabGroupBuilder TabGroup(std::string name);
} // namespace FlightUI
