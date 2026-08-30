#pragma once

#include "flightui/core/UIElement.hpp"

#include <memory>
#include <string>

namespace FlightUI {
class KeyValueGridBuilder {
public:
  // Lifetime
  explicit KeyValueGridBuilder(std::string id);
  KeyValueGridBuilder(const KeyValueGridBuilder &other);
  KeyValueGridBuilder(KeyValueGridBuilder &&other) noexcept;
  KeyValueGridBuilder &operator=(const KeyValueGridBuilder &other);
  KeyValueGridBuilder &operator=(KeyValueGridBuilder &&other) noexcept;
  ~KeyValueGridBuilder();

  // Explicit configuration and values
  KeyValueGridBuilder &SetColumnsPerRow(int columnsPerRow);
  KeyValueGridBuilder &SetLabelWidth(float width);
  KeyValueGridBuilder &SetEnabled(bool enabled);
  KeyValueGridBuilder &SetVisible(bool visible);
  KeyValueGridBuilder &SetTooltip(std::string tooltip);
  KeyValueGridBuilder &Add(std::string label, std::string value);
  KeyValueGridBuilder &AddDouble(std::string label, double value,
      std::string format);
  KeyValueGridBuilder &AddInt(std::string label, int value, std::string format);

  // Fluent configuration
  KeyValueGridBuilder &ColumnsPerRow(int columnsPerRow);
  KeyValueGridBuilder &LabelWidth(float width);
  KeyValueGridBuilder &Enabled(bool enabled);
  KeyValueGridBuilder &Visible(bool visible);
  KeyValueGridBuilder &Tooltip(std::string tooltip);

  // Element conversion
  operator UIElement() const;

private:
  // Implementation state
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};

KeyValueGridBuilder KeyValueGrid(std::string id);
} // namespace FlightUI
