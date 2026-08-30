#pragma once

#include "flightui/core/UIElement.hpp"

#include <memory>
#include <string>

namespace FlightUI {
enum class ToolbarAlignment {
  Left,
  Right,
};

class ToolbarBuilder {
public:
  // Lifetime
  ToolbarBuilder();
  ToolbarBuilder(const ToolbarBuilder &other);
  ToolbarBuilder(ToolbarBuilder &&other) noexcept;
  ToolbarBuilder &operator=(const ToolbarBuilder &other);
  ToolbarBuilder &operator=(ToolbarBuilder &&other) noexcept;
  ~ToolbarBuilder();

  // Explicit configuration
  ToolbarBuilder &SetAlignment(ToolbarAlignment alignment);
  ToolbarBuilder &SetCompact(bool compact = true);
  ToolbarBuilder &SetHeight(float height);
  ToolbarBuilder &SetSpacing(float spacing);
  ToolbarBuilder &SetId(std::string id);
  ToolbarBuilder &SetLeft(UIElement content);
  ToolbarBuilder &SetRight(UIElement content);

  // Fluent configuration
  ToolbarBuilder &AlignLeft();
  ToolbarBuilder &AlignRight();
  ToolbarBuilder &Compact(bool compact = true);
  ToolbarBuilder &Height(float height);
  ToolbarBuilder &Spacing(float spacing);
  ToolbarBuilder &Id(std::string id);
  ToolbarBuilder &Left(UIElement content);
  ToolbarBuilder &Right(UIElement content);

  // Child content
  UIElement operator[](UIElement child) const;
  UIElement operator[](Children children) const;
  UIElement operator[](ChildrenBuilder children) const;

  // Element conversion
  operator UIElement() const;

private:
  // Implementation state
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};

ToolbarBuilder Toolbar();
} // namespace FlightUI
