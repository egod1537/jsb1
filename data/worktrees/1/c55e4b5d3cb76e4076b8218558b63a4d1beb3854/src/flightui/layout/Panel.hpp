#pragma once

#include "flightui/core/UIElement.hpp"

#include <imgui.h>

#include <memory>
#include <string>

namespace FlightUI {
class PanelBuilder {
public:
  // Lifetime
  explicit PanelBuilder(std::string name);
  PanelBuilder(const PanelBuilder &other);
  PanelBuilder(PanelBuilder &&other) noexcept;
  PanelBuilder &operator=(const PanelBuilder &other);
  PanelBuilder &operator=(PanelBuilder &&other) noexcept;
  ~PanelBuilder();

  // Explicit configuration
  PanelBuilder &SetWidth(float width);
  PanelBuilder &SetHeight(float height);
  PanelBuilder &SetSize(Vector2 size);
  PanelBuilder &SetFlexibleWidth(bool flexible);
  PanelBuilder &SetFlexibleHeight(bool flexible);
  PanelBuilder &SetBorder(bool enabled);
  PanelBuilder &SetFlags(ImGuiChildFlags flags);
  PanelBuilder &SetEnabled(bool enabled);
  PanelBuilder &SetVisible(bool visible);
  PanelBuilder &SetTooltip(std::string tooltip);
  PanelBuilder &SetId(std::string id);

  // Fluent configuration
  PanelBuilder &Width(float width);
  PanelBuilder &Height(float height);
  PanelBuilder &Size(Vector2 size);
  PanelBuilder &FlexibleWidth(bool flexible);
  PanelBuilder &FlexibleHeight(bool flexible);
  PanelBuilder &Border(bool enabled);
  PanelBuilder &Flags(ImGuiChildFlags flags);
  PanelBuilder &Enabled(bool enabled);
  PanelBuilder &Visible(bool visible);
  PanelBuilder &Tooltip(std::string tooltip);
  PanelBuilder &Id(std::string id);

  // Child content
  UIElement operator[](UIElement child) const;
  UIElement operator[](Children children) const;
  UIElement operator[](ChildrenBuilder children) const;

private:
  // Implementation state
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};

PanelBuilder Panel(std::string name);
} // namespace FlightUI
