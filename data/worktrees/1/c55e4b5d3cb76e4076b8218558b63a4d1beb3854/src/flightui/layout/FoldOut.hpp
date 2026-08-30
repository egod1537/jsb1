#pragma once

#include "flightui/core/UIElement.hpp"

#include <imgui.h>

#include <memory>
#include <string>

namespace FlightUI {
enum class FoldOutVariant {
  Default,
  Section,
};

class FoldOutBuilder {
public:
  // Lifetime
  explicit FoldOutBuilder(std::string label);
  FoldOutBuilder(const FoldOutBuilder &other);
  FoldOutBuilder(FoldOutBuilder &&other) noexcept;
  FoldOutBuilder &operator=(const FoldOutBuilder &other);
  FoldOutBuilder &operator=(FoldOutBuilder &&other) noexcept;
  ~FoldOutBuilder();

  // Explicit configuration
  FoldOutBuilder &SetOpen(bool &isOpen);
  FoldOutBuilder &SetDefaultOpen(bool open);
  FoldOutBuilder &SetFlags(ImGuiTreeNodeFlags flags);
  FoldOutBuilder &SetFramed(bool enabled = true);
  FoldOutBuilder &SetSpanAvailWidth(bool enabled = true);
  FoldOutBuilder &SetVariant(FoldOutVariant variant);
  FoldOutBuilder &SetEnabled(bool enabled);
  FoldOutBuilder &SetVisible(bool visible);
  FoldOutBuilder &SetTooltip(std::string tooltip);
  FoldOutBuilder &SetId(std::string id);
  FoldOutBuilder &SetHeaderLeft(UIElement element, float width);
  FoldOutBuilder &SetHeaderRight(UIElement element, float width);

  // Fluent configuration
  FoldOutBuilder &Open(bool &isOpen);
  FoldOutBuilder &DefaultOpen(bool open = true);
  FoldOutBuilder &Flags(ImGuiTreeNodeFlags flags);
  FoldOutBuilder &Framed(bool enabled = true);
  FoldOutBuilder &SpanAvailWidth(bool enabled = true);
  FoldOutBuilder &Variant(FoldOutVariant variant);
  FoldOutBuilder &Section();
  FoldOutBuilder &Enabled(bool enabled);
  FoldOutBuilder &Visible(bool visible);
  FoldOutBuilder &Tooltip(std::string tooltip);
  FoldOutBuilder &Id(std::string id);
  FoldOutBuilder &HeaderLeft(UIElement element, float width);
  FoldOutBuilder &HeaderRight(UIElement element, float width);

  // Child content
  UIElement operator[](UIElement child) const;
  UIElement operator[](Children children) const;
  UIElement operator[](ChildrenBuilder children) const;

private:
  // Implementation state
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};

FoldOutBuilder FoldOut(std::string label);
} // namespace FlightUI
