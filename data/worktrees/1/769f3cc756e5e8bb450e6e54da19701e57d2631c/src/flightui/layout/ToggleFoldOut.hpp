#pragma once

#include "flightui/core/UIElement.hpp"
#include "flightui/layout/FoldOut.hpp"

#include <functional>
#include <memory>
#include <string>

namespace FlightUI {
using ToggleFoldOutChangedAction = std::function<void(bool)>;

class ToggleFoldOutBuilder {
public:
  // Lifetime
  ToggleFoldOutBuilder(std::string label, bool value);
  ToggleFoldOutBuilder(const ToggleFoldOutBuilder &other);
  ToggleFoldOutBuilder(ToggleFoldOutBuilder &&other) noexcept;
  ToggleFoldOutBuilder &operator=(const ToggleFoldOutBuilder &other);
  ToggleFoldOutBuilder &operator=(ToggleFoldOutBuilder &&other) noexcept;
  ~ToggleFoldOutBuilder();

  // Explicit configuration
  ToggleFoldOutBuilder &SetOpen(bool &isOpen);
  ToggleFoldOutBuilder &SetDefaultOpen(bool open = true);
  ToggleFoldOutBuilder &SetVariant(FoldOutVariant variant);
  ToggleFoldOutBuilder &SetToggleEnabled(bool enabled);
  ToggleFoldOutBuilder &SetVisible(bool visible);
  ToggleFoldOutBuilder &SetTooltip(std::string tooltip);
  ToggleFoldOutBuilder &SetId(std::string id);
  ToggleFoldOutBuilder &SetOnChanged(ToggleFoldOutChangedAction onChanged);

  // Fluent configuration
  ToggleFoldOutBuilder &Open(bool &isOpen);
  ToggleFoldOutBuilder &DefaultOpen(bool open = true);
  ToggleFoldOutBuilder &Variant(FoldOutVariant variant);
  ToggleFoldOutBuilder &Section();
  ToggleFoldOutBuilder &ToggleEnabled(bool enabled);
  ToggleFoldOutBuilder &Visible(bool visible);
  ToggleFoldOutBuilder &Tooltip(std::string tooltip);
  ToggleFoldOutBuilder &Id(std::string id);
  ToggleFoldOutBuilder &OnChanged(ToggleFoldOutChangedAction onChanged);

  // Child content
  UIElement operator[](UIElement child) const;
  UIElement operator[](Children children) const;
  UIElement operator[](ChildrenBuilder children) const;

private:
  // Implementation state
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};

ToggleFoldOutBuilder ToggleFoldOut(std::string label, bool value);
} // namespace FlightUI
