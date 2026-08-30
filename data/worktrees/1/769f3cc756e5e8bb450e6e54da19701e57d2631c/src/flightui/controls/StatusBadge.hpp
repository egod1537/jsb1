#pragma once

#include "flightui/core/Theme.hpp"
#include "flightui/core/UIElement.hpp"

#include <memory>
#include <string>

namespace FlightUI {
class StatusBadgeBuilder {
public:
  // Lifetime
  StatusBadgeBuilder(std::string label, StatusTone tone);
  StatusBadgeBuilder(const StatusBadgeBuilder &other);
  StatusBadgeBuilder(StatusBadgeBuilder &&other) noexcept;
  StatusBadgeBuilder &operator=(const StatusBadgeBuilder &other);
  StatusBadgeBuilder &operator=(StatusBadgeBuilder &&other) noexcept;
  ~StatusBadgeBuilder();

  // Explicit configuration
  StatusBadgeBuilder &SetTone(StatusTone tone);
  StatusBadgeBuilder &SetTooltip(std::string tooltip);

  // Fluent configuration
  StatusBadgeBuilder &Tone(StatusTone tone);
  StatusBadgeBuilder &Tooltip(std::string tooltip);

  // Element conversion
  operator UIElement() const;

private:
  // Implementation state
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};

StatusBadgeBuilder StatusBadge(std::string label, StatusTone tone);
} // namespace FlightUI
