#include "flightui/layout/Toolbar.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIScale.hpp"
#include "flightui/layout/HorizontalLayout.hpp"

#include <algorithm>
#include <imgui.h>
#include <utility>

namespace FlightUI {
class ToolbarBuilder::Impl {
public:
  ToolbarAlignment Alignment = ToolbarAlignment::Left;
  bool Compact = true;
  float Height = 28.0F;
  float Spacing = 4.0F;
  std::string Id;
  UIElement Left;
  UIElement Right;
};

ToolbarBuilder::ToolbarBuilder() : m_Impl(std::make_unique<Impl>()) {}

ToolbarBuilder::ToolbarBuilder(const ToolbarBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

ToolbarBuilder::ToolbarBuilder(ToolbarBuilder &&other) noexcept = default;

ToolbarBuilder &ToolbarBuilder::operator=(const ToolbarBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }
  return *this;
}

ToolbarBuilder &ToolbarBuilder::operator=(
    ToolbarBuilder &&other) noexcept = default;

ToolbarBuilder::~ToolbarBuilder() = default;

ToolbarBuilder &ToolbarBuilder::SetAlignment(ToolbarAlignment alignment) {
  m_Impl->Alignment = alignment;
  return *this;
}

ToolbarBuilder &ToolbarBuilder::SetCompact(bool compact) {
  m_Impl->Compact = compact;
  return *this;
}

ToolbarBuilder &ToolbarBuilder::SetHeight(float height) {
  m_Impl->Height = std::max(0.0F, height);
  return *this;
}

ToolbarBuilder &ToolbarBuilder::SetSpacing(float spacing) {
  m_Impl->Spacing = std::max(0.0F, spacing);
  return *this;
}

ToolbarBuilder &ToolbarBuilder::SetId(std::string id) {
  m_Impl->Id = std::move(id);
  return *this;
}

ToolbarBuilder &ToolbarBuilder::SetLeft(UIElement content) {
  m_Impl->Left = std::move(content);
  return *this;
}

ToolbarBuilder &ToolbarBuilder::SetRight(UIElement content) {
  m_Impl->Right = std::move(content);
  return *this;
}

ToolbarBuilder &ToolbarBuilder::AlignLeft() {
  return SetAlignment(ToolbarAlignment::Left);
}

ToolbarBuilder &ToolbarBuilder::AlignRight() {
  return SetAlignment(ToolbarAlignment::Right);
}

ToolbarBuilder &ToolbarBuilder::Compact(bool compact) {
  return SetCompact(compact);
}

ToolbarBuilder &ToolbarBuilder::Height(float height) {
  return SetHeight(height);
}

ToolbarBuilder &ToolbarBuilder::Spacing(float spacing) {
  return SetSpacing(spacing);
}

ToolbarBuilder &ToolbarBuilder::Id(std::string id) {
  return SetId(std::move(id));
}

ToolbarBuilder &ToolbarBuilder::Left(UIElement content) {
  return SetLeft(std::move(content));
}

ToolbarBuilder &ToolbarBuilder::Right(UIElement content) {
  return SetRight(std::move(content));
}

UIElement ToolbarBuilder::operator[](UIElement child) const {
  Children children;
  children.push_back(std::move(child));
  return (*this)[std::move(children)];
}

UIElement ToolbarBuilder::operator[](ChildrenBuilder children) const {
  return (*this)[std::move(children).TakeChildren()];
}

UIElement ToolbarBuilder::operator[](Children children) const {
  ToolbarBuilder builder(*this);
  UIElement content =
      HorizontalLayout(std::move(children)).Spacing(builder.m_Impl->Spacing);
  if (builder.m_Impl->Alignment == ToolbarAlignment::Right) {
    builder.m_Impl->Right = std::move(content);
  } else {
    builder.m_Impl->Left = std::move(content);
  }
  return builder;
}

ToolbarBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  return CreateElement([state] {
    constexpr ImGuiTableFlags Flags =
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings
        | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoBordersInBody;
    const ImVec2 previousFramePadding = ImGui::GetStyle().FramePadding;
    if (state.Compact) {
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
          ImVec2(previousFramePadding.x, Ui(3.0F)));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0F, 0.0F));

    const ImVec2 toolbarMinimum = ImGui::GetCursorScreenPos();
    const ImVec2 toolbarMaximum{
        toolbarMinimum.x + std::max(ImGui::GetContentRegionAvail().x, 1.0F),
        toolbarMinimum.y + Ui(state.Height)};
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(toolbarMinimum,
        toolbarMaximum,
        ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    drawList->AddLine(ImVec2(toolbarMinimum.x, toolbarMaximum.y),
        toolbarMaximum,
        ImGui::GetColorU32(ImGuiCol_Border));

    const std::string tableId = "##FlightUIToolbar" + state.Id;
    if (ImGui::BeginTable(tableId.c_str(), 2, Flags)) {
      ImGui::TableSetupColumn("##Stretch",
          ImGuiTableColumnFlags_WidthStretch,
          1.0F);
      ImGui::TableSetupColumn("##Content",
          ImGuiTableColumnFlags_WidthFixed,
          0.0F);
      ImGui::TableNextRow(ImGuiTableRowFlags_None, Ui(state.Height));
      ImGui::TableSetColumnIndex(0);
      state.Left.Render();
      ImGui::TableSetColumnIndex(1);
      state.Right.Render();
      ImGui::EndTable();
    }

    ImGui::PopStyleVar();
    if (state.Compact) {
      ImGui::PopStyleVar();
    }
  });
}

ToolbarBuilder Toolbar() { return ToolbarBuilder(); }
} // namespace FlightUI
