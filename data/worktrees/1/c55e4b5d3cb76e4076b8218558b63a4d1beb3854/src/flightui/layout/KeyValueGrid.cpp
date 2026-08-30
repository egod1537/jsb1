#include "flightui/layout/KeyValueGrid.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIRenderHelpers.hpp"
#include "flightui/core/UIScale.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <imgui.h>
#include <utility>
#include <vector>

namespace FlightUI {
namespace {
struct KeyValueItem {
  std::string Label;
  std::string Value;
};

std::string FormatDouble(double value, const std::string &format) {
  std::array<char, 128> buffer{};
  std::snprintf(buffer.data(), buffer.size(), format.c_str(), value);
  return buffer.data();
}

std::string FormatInt(int value, const std::string &format) {
  std::array<char, 128> buffer{};
  std::snprintf(buffer.data(), buffer.size(), format.c_str(), value);
  return buffer.data();
}
} // namespace

class KeyValueGridBuilder::Impl {
public:
  std::string Id;
  std::vector<KeyValueItem> Items;
  int ColumnsPerRow = 2;
  float LabelWidth = 120.0F;
  bool Enabled = true;
  bool Visible = true;
  std::string Tooltip;
};

KeyValueGridBuilder::KeyValueGridBuilder(std::string id)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Id = std::move(id);
}

KeyValueGridBuilder::KeyValueGridBuilder(const KeyValueGridBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

KeyValueGridBuilder::KeyValueGridBuilder(
    KeyValueGridBuilder &&other) noexcept = default;

KeyValueGridBuilder &KeyValueGridBuilder::operator=(
    const KeyValueGridBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

KeyValueGridBuilder &KeyValueGridBuilder::operator=(
    KeyValueGridBuilder &&other) noexcept = default;

KeyValueGridBuilder::~KeyValueGridBuilder() = default;

KeyValueGridBuilder &KeyValueGridBuilder::SetColumnsPerRow(int columnsPerRow) {
  m_Impl->ColumnsPerRow = std::max(1, columnsPerRow);
  return *this;
}

KeyValueGridBuilder &KeyValueGridBuilder::SetLabelWidth(float width) {
  m_Impl->LabelWidth = width;
  return *this;
}

KeyValueGridBuilder &KeyValueGridBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

KeyValueGridBuilder &KeyValueGridBuilder::SetVisible(bool visible) {
  m_Impl->Visible = visible;
  return *this;
}

KeyValueGridBuilder &KeyValueGridBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

KeyValueGridBuilder &KeyValueGridBuilder::Add(std::string label,
    std::string value) {
  m_Impl->Items.push_back({std::move(label), std::move(value)});
  return *this;
}

KeyValueGridBuilder &KeyValueGridBuilder::AddDouble(std::string label,
    double value, std::string format) {
  return Add(std::move(label), FormatDouble(value, format));
}

KeyValueGridBuilder &KeyValueGridBuilder::AddInt(std::string label, int value,
    std::string format) {
  return Add(std::move(label), FormatInt(value, format));
}

KeyValueGridBuilder &KeyValueGridBuilder::ColumnsPerRow(int columnsPerRow) {
  return SetColumnsPerRow(columnsPerRow);
}

KeyValueGridBuilder &KeyValueGridBuilder::LabelWidth(float width) {
  return SetLabelWidth(width);
}

KeyValueGridBuilder &KeyValueGridBuilder::Enabled(bool enabled) {
  return SetEnabled(enabled);
}

KeyValueGridBuilder &KeyValueGridBuilder::Visible(bool visible) {
  return SetVisible(visible);
}

KeyValueGridBuilder &KeyValueGridBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

KeyValueGridBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  return CreateElement([state] {
    if (!state.Visible) {
      return;
    }

    const int columnsPerRow = std::max(1, state.ColumnsPerRow);
    const int tableColumnCount = columnsPerRow * 2;
    constexpr ImGuiTableFlags Flags = ImGuiTableFlags_SizingStretchProp
                                      | ImGuiTableFlags_NoSavedSettings
                                      | ImGuiTableFlags_PadOuterX;
    Internal::DisabledScope disabledScope(!state.Enabled);

    if (!ImGui::BeginTable(state.Id.c_str(), tableColumnCount, Flags)) {
      return;
    }

    Internal::ShowTooltipIfHovered(state.Tooltip);

    for (int column = 0; column < tableColumnCount; ++column) {
      const bool isLabelColumn = column % 2 == 0;
      ImGui::TableSetupColumn(nullptr,
          isLabelColumn ? ImGuiTableColumnFlags_WidthFixed
                        : ImGuiTableColumnFlags_WidthStretch,
          isLabelColumn ? Ui(state.LabelWidth) : 1.0F);
    }

    for (std::size_t index = 0; index < state.Items.size(); ++index) {
      if (index % static_cast<std::size_t>(columnsPerRow) == 0) {
        ImGui::TableNextRow();
      }

      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", state.Items[index].Label.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(state.Items[index].Value.c_str());
    }

    ImGui::EndTable();
  });
}

KeyValueGridBuilder KeyValueGrid(std::string id) {
  return KeyValueGridBuilder(std::move(id));
}
} // namespace FlightUI
