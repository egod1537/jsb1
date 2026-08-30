#include "flightui/layout/PropertyTable.hpp"

#include "flightui/core/Theme.hpp"
#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIRenderHelpers.hpp"
#include "flightui/core/UIScale.hpp"

#include <algorithm>
#include <cfloat>
#include <imgui.h>
#include <utility>
#include <vector>

namespace FlightUI {
namespace {
struct PropertyRowState {
  std::string Label;
  UIElement Content;
  std::string Tooltip;
};
} // namespace

PropertyGridLayout ResolvePropertyGridLayout(float availableWidth,
    float singleColumnThreshold) {
  return availableWidth >= singleColumnThreshold
             ? PropertyGridLayout::TwoColumns
             : PropertyGridLayout::SingleColumn;
}

bool IsAlternatePropertyRow(std::size_t index) { return index % 2 != 0; }

class PropertyRowBuilder::Impl {
public:
  std::string Label;
  UIElement Content;
  std::string Tooltip;
};

class PropertyTableBuilder::Impl {
public:
  std::string Id;
  std::vector<PropertyRowState> Rows;
  float LabelWidth = 120.0F;
  float LabelWidthRatio = 0.38F;
  float MinimumLabelWidth = 100.0F;
  float MaximumLabelWidth = 180.0F;
  float SingleColumnThreshold = 320.0F;
  float ColumnSpacing = 4.0F;
  float RowPadding = 2.0F;
  bool HasFixedLabelWidth = false;
  bool AlternatingRows = false;
  bool Enabled = true;
  bool Visible = true;
  std::string Tooltip;
};

PropertyRowBuilder::PropertyRowBuilder(std::string label)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Label = std::move(label);
}

PropertyRowBuilder::PropertyRowBuilder(const PropertyRowBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

PropertyRowBuilder::PropertyRowBuilder(
    PropertyRowBuilder &&other) noexcept = default;

PropertyRowBuilder &PropertyRowBuilder::operator=(
    const PropertyRowBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }
  return *this;
}

PropertyRowBuilder &PropertyRowBuilder::operator=(
    PropertyRowBuilder &&other) noexcept = default;

PropertyRowBuilder::~PropertyRowBuilder() = default;

PropertyRowBuilder &PropertyRowBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

PropertyRowBuilder &PropertyRowBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

PropertyRowBuilder PropertyRowBuilder::operator[](UIElement content) const {
  PropertyRowBuilder row(*this);
  row.m_Impl->Content = std::move(content);
  return row;
}

PropertyTableBuilder::PropertyTableBuilder(std::string id)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Id = std::move(id);
}

PropertyTableBuilder::PropertyTableBuilder(const PropertyTableBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

PropertyTableBuilder::PropertyTableBuilder(
    PropertyTableBuilder &&other) noexcept = default;

PropertyTableBuilder &PropertyTableBuilder::operator=(
    const PropertyTableBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }
  return *this;
}

PropertyTableBuilder &PropertyTableBuilder::operator=(
    PropertyTableBuilder &&other) noexcept = default;

PropertyTableBuilder::~PropertyTableBuilder() = default;

PropertyTableBuilder &PropertyTableBuilder::SetLabelWidth(float width) {
  m_Impl->LabelWidth = std::max(0.0F, width);
  m_Impl->HasFixedLabelWidth = true;
  return *this;
}

PropertyTableBuilder &PropertyTableBuilder::SetLabelWidthRatio(float ratio) {
  m_Impl->LabelWidthRatio = std::clamp(ratio, 0.0F, 1.0F);
  m_Impl->HasFixedLabelWidth = false;
  return *this;
}

PropertyTableBuilder &PropertyTableBuilder::SetMinimumLabelWidth(float width) {
  m_Impl->MinimumLabelWidth = std::max(0.0F, width);
  return *this;
}

PropertyTableBuilder &PropertyTableBuilder::SetMaximumLabelWidth(float width) {
  m_Impl->MaximumLabelWidth = std::max(0.0F, width);
  return *this;
}

PropertyTableBuilder &PropertyTableBuilder::SetSingleColumnThreshold(
    float width) {
  m_Impl->SingleColumnThreshold = std::max(0.0F, width);
  return *this;
}

PropertyTableBuilder &PropertyTableBuilder::SetColumnSpacing(float spacing) {
  m_Impl->ColumnSpacing = std::max(0.0F, spacing);
  return *this;
}

PropertyTableBuilder &PropertyTableBuilder::SetRowPadding(float padding) {
  m_Impl->RowPadding = std::max(0.0F, padding);
  return *this;
}

PropertyTableBuilder &PropertyTableBuilder::SetAlternatingRows(bool enabled) {
  m_Impl->AlternatingRows = enabled;
  return *this;
}

PropertyTableBuilder &PropertyTableBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

PropertyTableBuilder &PropertyTableBuilder::SetVisible(bool visible) {
  m_Impl->Visible = visible;
  return *this;
}

PropertyTableBuilder &PropertyTableBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

PropertyTableBuilder &PropertyTableBuilder::Add(std::string label,
    UIElement content) {
  m_Impl->Rows.push_back({std::move(label), std::move(content), {}});
  return *this;
}

PropertyTableBuilder &PropertyTableBuilder::Add(PropertyRowBuilder row) {
  m_Impl->Rows.push_back({std::move(row.m_Impl->Label),
      std::move(row.m_Impl->Content),
      std::move(row.m_Impl->Tooltip)});
  return *this;
}

PropertyTableBuilder &PropertyTableBuilder::LabelWidth(float width) {
  return SetLabelWidth(width);
}

PropertyTableBuilder &PropertyTableBuilder::LabelWidthRatio(float ratio) {
  return SetLabelWidthRatio(ratio);
}

PropertyTableBuilder &PropertyTableBuilder::MinimumLabelWidth(float width) {
  return SetMinimumLabelWidth(width);
}

PropertyTableBuilder &PropertyTableBuilder::MaximumLabelWidth(float width) {
  return SetMaximumLabelWidth(width);
}

PropertyTableBuilder &PropertyTableBuilder::SingleColumnThreshold(float width) {
  return SetSingleColumnThreshold(width);
}

PropertyTableBuilder &PropertyTableBuilder::ColumnSpacing(float spacing) {
  return SetColumnSpacing(spacing);
}

PropertyTableBuilder &PropertyTableBuilder::RowPadding(float padding) {
  return SetRowPadding(padding);
}

PropertyTableBuilder &PropertyTableBuilder::AlternatingRows(bool enabled) {
  return SetAlternatingRows(enabled);
}

PropertyTableBuilder &PropertyTableBuilder::Enabled(bool enabled) {
  return SetEnabled(enabled);
}

PropertyTableBuilder &PropertyTableBuilder::Visible(bool visible) {
  return SetVisible(visible);
}

PropertyTableBuilder &PropertyTableBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

PropertyTableBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  return CreateElement([state] {
    if (!state.Visible) {
      return;
    }

    constexpr ImGuiTableFlags Flags = ImGuiTableFlags_SizingStretchProp
                                      | ImGuiTableFlags_NoSavedSettings
                                      | ImGuiTableFlags_NoPadOuterX;
    Internal::DisabledScope disabledScope(!state.Enabled);
    const ImVec2 cellPadding{Ui(state.ColumnSpacing * 0.5F),
        Ui(state.RowPadding)};
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cellPadding);

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const bool useTwoColumns = ResolvePropertyGridLayout(availableWidth,
                                   Ui(state.SingleColumnThreshold))
                               == PropertyGridLayout::TwoColumns;
    const int columnCount = useTwoColumns ? 2 : 1;
    if (!ImGui::BeginTable(state.Id.c_str(), columnCount, Flags)) {
      ImGui::PopStyleVar();
      return;
    }

    Internal::ShowTooltipIfHovered(state.Tooltip);
    if (useTwoColumns) {
      const float minimumLabelWidth = Ui(state.MinimumLabelWidth);
      const float maximumLabelWidth =
          std::max(minimumLabelWidth, Ui(state.MaximumLabelWidth));
      const float labelWidth =
          state.HasFixedLabelWidth
              ? Ui(state.LabelWidth)
              : std::clamp(availableWidth * state.LabelWidthRatio,
                    minimumLabelWidth,
                    maximumLabelWidth);
      ImGui::TableSetupColumn("Label",
          ImGuiTableColumnFlags_WidthFixed,
          labelWidth);
      ImGui::TableSetupColumn("Value",
          ImGuiTableColumnFlags_WidthStretch,
          1.0F);
    } else {
      ImGui::TableSetupColumn("Property",
          ImGuiTableColumnFlags_WidthStretch,
          1.0F);
    }

    for (std::size_t index = 0; index < state.Rows.size(); ++index) {
      const ImU32 rowColor = ImGui::ColorConvertFloat4ToU32(
          GetThemeColor(IsAlternatePropertyRow(index)
                            ? ThemeColor::PropertyRowBackgroundAlternate
                            : ThemeColor::PropertyRowBackground));
      ImGui::TableNextRow();
      if (state.AlternatingRows) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowColor);
      }

      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextDisabled("%s", state.Rows[index].Label.c_str());
      Internal::ShowTooltipIfHovered(state.Rows[index].Tooltip);
      if (useTwoColumns) {
        ImGui::TableSetColumnIndex(1);
      } else {
        ImGui::TableNextRow();
        if (state.AlternatingRows) {
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowColor);
        }
        ImGui::TableSetColumnIndex(0);
      }

      ImGui::PushID(static_cast<int>(index));
      ImGui::PushItemWidth(-FLT_MIN);
      state.Rows[index].Content.Render();
      ImGui::PopItemWidth();
      ImGui::PopID();
    }

    ImGui::EndTable();
    ImGui::PopStyleVar();
  });
}

PropertyTableBuilder PropertyTable(std::string id) {
  return PropertyTableBuilder(std::move(id));
}

PropertyGridBuilder PropertyGrid(std::string id) {
  return PropertyGridBuilder(std::move(id));
}

PropertyRowBuilder PropertyRow(std::string label) {
  return PropertyRowBuilder(std::move(label));
}
} // namespace FlightUI
