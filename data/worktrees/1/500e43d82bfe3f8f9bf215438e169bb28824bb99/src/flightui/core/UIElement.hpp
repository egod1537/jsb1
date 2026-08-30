#pragma once

#include "flightui/core/UICommon.hpp"

#include <memory>
#include <vector>

namespace FlightUI {
class UIElement {
public:
  // Lifetime
  UIElement();
  UIElement(const UIElement &other);
  UIElement(UIElement &&other) noexcept;
  UIElement &operator=(const UIElement &other);
  UIElement &operator=(UIElement &&other) noexcept;
  ~UIElement();

  // Rendering and validity
  void Render() const;
  bool IsValid() const;

private:
  // Shared implementation
  class Impl;
  std::shared_ptr<Impl> m_Impl;

  explicit UIElement(std::shared_ptr<Impl> impl);

  friend UIElement CreateElement(Action renderAction);
};

using Children = std::vector<UIElement>;

class ChildrenBuilder {
public:
  // Construction and composition
  ChildrenBuilder();
  explicit ChildrenBuilder(UIElement child);

  ChildrenBuilder operator+(UIElement child) const;

  // Child access
  const Children &GetChildren() const;
  Children TakeChildren() &&;

private:
  Children children_;
};

ChildrenBuilder operator+(UIElement child);
} // namespace FlightUI
