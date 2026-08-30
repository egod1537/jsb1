#pragma once

#include "flightui/core/UIElement.hpp"

#include <memory>
#include <cstddef>
#include <string>

namespace FlightUI {
enum class PropertyGridLayout {
  SingleColumn,
  TwoColumns,
};

PropertyGridLayout ResolvePropertyGridLayout(float availableWidth,
    float singleColumnThreshold);
bool IsAlternatePropertyRow(std::size_t index);

class PropertyRowBuilder {
public:
  // Lifetime
  explicit PropertyRowBuilder(std::string label);
  PropertyRowBuilder(const PropertyRowBuilder &other);
  PropertyRowBuilder(PropertyRowBuilder &&other) noexcept;
  PropertyRowBuilder &operator=(const PropertyRowBuilder &other);
  PropertyRowBuilder &operator=(PropertyRowBuilder &&other) noexcept;
  ~PropertyRowBuilder();

  // Row content
  PropertyRowBuilder &SetTooltip(std::string tooltip);
  PropertyRowBuilder &Tooltip(std::string tooltip);
  PropertyRowBuilder operator[](UIElement content) const;

private:
  friend class PropertyTableBuilder;

  // Implementation state
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};

class PropertyTableBuilder {
public:
  // Lifetime
  explicit PropertyTableBuilder(std::string id);
  PropertyTableBuilder(const PropertyTableBuilder &other);
  PropertyTableBuilder(PropertyTableBuilder &&other) noexcept;
  PropertyTableBuilder &operator=(const PropertyTableBuilder &other);
  PropertyTableBuilder &operator=(PropertyTableBuilder &&other) noexcept;
  ~PropertyTableBuilder();

  // Explicit configuration and rows
  PropertyTableBuilder &SetLabelWidth(float width);
  PropertyTableBuilder &SetLabelWidthRatio(float ratio);
  PropertyTableBuilder &SetMinimumLabelWidth(float width);
  PropertyTableBuilder &SetMaximumLabelWidth(float width);
  PropertyTableBuilder &SetSingleColumnThreshold(float width);
  PropertyTableBuilder &SetColumnSpacing(float spacing);
  PropertyTableBuilder &SetRowPadding(float padding);
  PropertyTableBuilder &SetAlternatingRows(bool enabled = true);
  PropertyTableBuilder &SetEnabled(bool enabled);
  PropertyTableBuilder &SetVisible(bool visible);
  PropertyTableBuilder &SetTooltip(std::string tooltip);
  PropertyTableBuilder &Add(std::string label, UIElement content);
  PropertyTableBuilder &Add(PropertyRowBuilder row);

  // Fluent configuration
  PropertyTableBuilder &LabelWidth(float width);
  PropertyTableBuilder &LabelWidthRatio(float ratio);
  PropertyTableBuilder &MinimumLabelWidth(float width);
  PropertyTableBuilder &MaximumLabelWidth(float width);
  PropertyTableBuilder &SingleColumnThreshold(float width);
  PropertyTableBuilder &ColumnSpacing(float spacing);
  PropertyTableBuilder &RowPadding(float padding);
  PropertyTableBuilder &AlternatingRows(bool enabled = true);
  PropertyTableBuilder &Enabled(bool enabled);
  PropertyTableBuilder &Visible(bool visible);
  PropertyTableBuilder &Tooltip(std::string tooltip);

  // Element conversion
  operator UIElement() const;

private:
  // Implementation state
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};

using PropertyGridBuilder = PropertyTableBuilder;

PropertyTableBuilder PropertyTable(std::string id);
PropertyGridBuilder PropertyGrid(std::string id);
PropertyRowBuilder PropertyRow(std::string label);
} // namespace FlightUI
