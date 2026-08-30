#pragma once

#include "flightui/core/UIElement.hpp"

#include <functional>
#include <imgui.h>
#include <memory>
#include <string>

namespace FlightUI {
using IconButtonAction = std::function<void()>;
using ToggleIconButtonChangedAction = std::function<void(bool)>;

class IconButtonBuilder {
public:
  // Lifetime
  IconButtonBuilder(std::string id, ImTextureID texture);
  IconButtonBuilder(const IconButtonBuilder &other);
  IconButtonBuilder(IconButtonBuilder &&other) noexcept;
  IconButtonBuilder &operator=(const IconButtonBuilder &other);
  IconButtonBuilder &operator=(IconButtonBuilder &&other) noexcept;
  ~IconButtonBuilder();

  // Explicit configuration
  IconButtonBuilder &SetFallbackText(std::string text);
  IconButtonBuilder &SetSize(float size);
  IconButtonBuilder &SetSelected(bool selected);
  IconButtonBuilder &SetToggle(bool toggle = true);
  IconButtonBuilder &SetEnabled(bool enabled);
  IconButtonBuilder &SetTooltip(std::string tooltip);
  IconButtonBuilder &SetOnAction(IconButtonAction onAction);
  IconButtonBuilder &SetOnChanged(ToggleIconButtonChangedAction onChanged);

  // Fluent configuration
  IconButtonBuilder &FallbackText(std::string text);
  IconButtonBuilder &Size(float size);
  IconButtonBuilder &Selected(bool selected = true);
  IconButtonBuilder &Toggle(bool toggle = true);
  IconButtonBuilder &Enabled(bool enabled);
  IconButtonBuilder &Tooltip(std::string tooltip);
  IconButtonBuilder &OnAction(IconButtonAction onAction);
  IconButtonBuilder &OnChanged(ToggleIconButtonChangedAction onChanged);

  // Element conversion
  operator UIElement() const;

private:
  // Implementation state
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};

IconButtonBuilder IconButton(std::string id, ImTextureID texture);
IconButtonBuilder ToggleIconButton(std::string id, ImTextureID texture,
    bool selected);
} // namespace FlightUI
