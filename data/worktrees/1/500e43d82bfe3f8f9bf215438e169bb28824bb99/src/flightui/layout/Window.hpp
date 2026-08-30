#pragma once

#include "flightui/core/UIElement.hpp"

#include <imgui.h>

#include <memory>
#include <string>

namespace FlightUI {
class WindowBuilder {
public:
  // Lifetime
  explicit WindowBuilder(std::string title);
  WindowBuilder(const WindowBuilder &other);
  WindowBuilder(WindowBuilder &&other) noexcept;
  WindowBuilder &operator=(const WindowBuilder &other);
  WindowBuilder &operator=(WindowBuilder &&other) noexcept;
  ~WindowBuilder();

  // Explicit configuration
  WindowBuilder &SetOpen(bool &isOpen);
  WindowBuilder &SetFlags(ImGuiWindowFlags flags);
  WindowBuilder &SetInitialSize(Vector2 size);
  WindowBuilder &SetInitialPosition(Vector2 position);
  WindowBuilder &SetEnabled(bool enabled);
  WindowBuilder &SetVisible(bool visible);
  WindowBuilder &SetTooltip(std::string tooltip);
  WindowBuilder &SetId(std::string id);

  // Fluent configuration
  WindowBuilder &Open(bool &isOpen);
  WindowBuilder &Flags(ImGuiWindowFlags flags);
  WindowBuilder &InitialSize(Vector2 size);
  WindowBuilder &InitialPosition(Vector2 position);
  WindowBuilder &Enabled(bool enabled);
  WindowBuilder &Visible(bool visible);
  WindowBuilder &Tooltip(std::string tooltip);
  WindowBuilder &Id(std::string id);

  // Child content
  UIElement operator[](UIElement child) const;
  UIElement operator[](Children children) const;
  UIElement operator[](ChildrenBuilder children) const;

private:
  // Implementation state
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};

WindowBuilder Window(std::string title);
} // namespace FlightUI
