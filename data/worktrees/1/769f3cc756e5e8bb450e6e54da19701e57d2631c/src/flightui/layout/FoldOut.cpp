#include "flightui/layout/FoldOut.hpp"

#include "flightui/core/Theme.hpp"
#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIRenderHelpers.hpp"
#include "flightui/core/UIScale.hpp"

#include <imgui.h>

#include <algorithm>
#include <utility>

namespace FlightUI {
class FoldOutBuilder::Impl {
public:
  std::string Label;
  bool *Open = nullptr;
  bool DefaultOpen = false;
  ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_None;
  FoldOutVariant Variant = FoldOutVariant::Default;
  bool Enabled = true;
  bool Visible = true;
  std::string Tooltip;
  std::string Id;
  UIElement HeaderLeft;
  float HeaderLeftWidth = 0.0F;
  UIElement HeaderRight;
  float HeaderRightWidth = 0.0F;
};

namespace {
std::string MakeFoldOutLabel(const std::string &label, const std::string &id) {
  if (id.empty()) {
    return label;
  }

  return label + "###" + id;
}

std::string MakeFoldOutIdLabel(
    const std::string &label, const std::string &id) {
  return "###" + (id.empty() ? label : id);
}

bool ShouldTreePop(ImGuiTreeNodeFlags flags) {
  return (flags & ImGuiTreeNodeFlags_NoTreePushOnOpen) == 0;
}
} // namespace

FoldOutBuilder::FoldOutBuilder(std::string label)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Label = std::move(label);
}

FoldOutBuilder::FoldOutBuilder(const FoldOutBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

FoldOutBuilder::FoldOutBuilder(FoldOutBuilder &&other) noexcept = default;

FoldOutBuilder &FoldOutBuilder::operator=(const FoldOutBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

FoldOutBuilder &FoldOutBuilder::operator=(
    FoldOutBuilder &&other) noexcept = default;

FoldOutBuilder::~FoldOutBuilder() = default;

FoldOutBuilder &FoldOutBuilder::SetOpen(bool &isOpen) {
  m_Impl->Open = &isOpen;
  return *this;
}

FoldOutBuilder &FoldOutBuilder::SetDefaultOpen(bool open) {
  m_Impl->DefaultOpen = open;
  return *this;
}

FoldOutBuilder &FoldOutBuilder::SetFlags(ImGuiTreeNodeFlags flags) {
  m_Impl->Flags = flags;
  return *this;
}

FoldOutBuilder &FoldOutBuilder::SetFramed(bool enabled) {
  if (enabled) {
    m_Impl->Flags |= ImGuiTreeNodeFlags_Framed;
  } else {
    m_Impl->Flags &= ~ImGuiTreeNodeFlags_Framed;
  }

  return *this;
}

FoldOutBuilder &FoldOutBuilder::SetSpanAvailWidth(bool enabled) {
  if (enabled) {
    m_Impl->Flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
  } else {
    m_Impl->Flags &= ~ImGuiTreeNodeFlags_SpanAvailWidth;
  }

  return *this;
}

FoldOutBuilder &FoldOutBuilder::SetVariant(FoldOutVariant variant) {
  m_Impl->Variant = variant;
  if (variant == FoldOutVariant::Section) {
    SetFramed();
    SetSpanAvailWidth();
  }
  return *this;
}

FoldOutBuilder &FoldOutBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

FoldOutBuilder &FoldOutBuilder::SetVisible(bool visible) {
  m_Impl->Visible = visible;
  return *this;
}

FoldOutBuilder &FoldOutBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

FoldOutBuilder &FoldOutBuilder::SetId(std::string id) {
  m_Impl->Id = std::move(id);
  return *this;
}

FoldOutBuilder &FoldOutBuilder::SetHeaderLeft(
    UIElement element, float width) {
  m_Impl->HeaderLeft = std::move(element);
  m_Impl->HeaderLeftWidth = width;
  return *this;
}

FoldOutBuilder &FoldOutBuilder::SetHeaderRight(
    UIElement element, float width) {
  m_Impl->HeaderRight = std::move(element);
  m_Impl->HeaderRightWidth = width;
  return *this;
}

FoldOutBuilder &FoldOutBuilder::Open(bool &isOpen) { return SetOpen(isOpen); }

FoldOutBuilder &FoldOutBuilder::DefaultOpen(bool open) {
  return SetDefaultOpen(open);
}

FoldOutBuilder &FoldOutBuilder::Flags(ImGuiTreeNodeFlags flags) {
  return SetFlags(flags);
}

FoldOutBuilder &FoldOutBuilder::Framed(bool enabled) {
  return SetFramed(enabled);
}

FoldOutBuilder &FoldOutBuilder::SpanAvailWidth(bool enabled) {
  return SetSpanAvailWidth(enabled);
}

FoldOutBuilder &FoldOutBuilder::Variant(FoldOutVariant variant) {
  return SetVariant(variant);
}

FoldOutBuilder &FoldOutBuilder::Section() {
  return SetVariant(FoldOutVariant::Section);
}

FoldOutBuilder &FoldOutBuilder::Enabled(bool enabled) {
  return SetEnabled(enabled);
}

FoldOutBuilder &FoldOutBuilder::Visible(bool visible) {
  return SetVisible(visible);
}

FoldOutBuilder &FoldOutBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

FoldOutBuilder &FoldOutBuilder::Id(std::string id) {
  return SetId(std::move(id));
}

FoldOutBuilder &FoldOutBuilder::HeaderLeft(
    UIElement element, float width) {
  return SetHeaderLeft(std::move(element), width);
}

FoldOutBuilder &FoldOutBuilder::HeaderRight(
    UIElement element, float width) {
  return SetHeaderRight(std::move(element), width);
}

UIElement FoldOutBuilder::operator[](UIElement child) const {
  Children children;
  children.push_back(std::move(child));
  return (*this)[std::move(children)];
}

UIElement FoldOutBuilder::operator[](ChildrenBuilder children) const {
  return (*this)[std::move(children).TakeChildren()];
}

UIElement FoldOutBuilder::operator[](Children children) const {
  Impl state = *m_Impl;
  return CreateElement([state, children = std::move(children)] {
    if (!state.Visible) {
      return;
    }

    if (state.Open != nullptr) {
      ImGui::SetNextItemOpen(*state.Open, ImGuiCond_Always);
    } else if (state.DefaultOpen) {
      ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    }

    Internal::DisabledScope disabledScope(!state.Enabled);
    const bool hasHeaderLeft = state.HeaderLeft.IsValid();
    const bool hasHeaderRight = state.HeaderRight.IsValid();
    const std::string label = hasHeaderLeft
                                  ? MakeFoldOutIdLabel(state.Label, state.Id)
                                  : MakeFoldOutLabel(state.Label, state.Id);
    if (hasHeaderLeft || hasHeaderRight) {
      ImGui::SetNextItemAllowOverlap();
    }
    const bool isSection = state.Variant == FoldOutVariant::Section;
    if (isSection) {
      ImGui::PushStyleColor(ImGuiCol_Header,
          GetThemeColor(ThemeColor::FoldOutSectionBackground));
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
          GetThemeColor(ThemeColor::FoldOutSectionBackgroundHovered));
      ImGui::PushStyleColor(ImGuiCol_HeaderActive,
          GetThemeColor(ThemeColor::FoldOutSectionBackgroundActive));
    }
    const bool isOpen = ImGui::TreeNodeEx(label.c_str(), state.Flags);
    if (isSection) {
      ImGui::PopStyleColor(3);
    }

    Internal::ShowTooltipIfHovered(state.Tooltip);

    const ImVec2 headerMinimum = ImGui::GetItemRectMin();
    const ImVec2 headerMaximum = ImGui::GetItemRectMax();
    const ImVec2 contentCursor = ImGui::GetCursorScreenPos();

    if (hasHeaderLeft) {
      const float controlWidth = Ui(state.HeaderLeftWidth);
      const float controlHeight = ImGui::GetFrameHeight();
      const float controlX =
          headerMinimum.x + ImGui::GetTreeNodeToLabelSpacing();
      ImGui::SetCursorScreenPos(ImVec2(controlX,
          headerMinimum.y
              + std::max((headerMaximum.y - headerMinimum.y - controlHeight)
                             * 0.5F,
                  0.0F)));
      ImGui::PushID(state.Id.empty() ? state.Label.c_str()
                                     : state.Id.c_str());
      state.HeaderLeft.Render();
      ImGui::PopID();

      const float titleX = controlX + controlWidth
                           + ImGui::GetStyle().ItemInnerSpacing.x;
      const float titleY = headerMinimum.y
                           + std::max((headerMaximum.y - headerMinimum.y
                                          - ImGui::GetTextLineHeight())
                                          * 0.5F,
                               0.0F);
      const float titleMaximumX = hasHeaderRight
                                      ? headerMaximum.x
                                            - Ui(state.HeaderRightWidth)
                                      : headerMaximum.x;
      ImGui::GetWindowDrawList()->PushClipRect(
          ImVec2(titleX, headerMinimum.y),
          ImVec2(titleMaximumX, headerMaximum.y),
          true);
      ImGui::GetWindowDrawList()->AddText(ImVec2(titleX, titleY),
          ImGui::GetColorU32(state.Enabled ? ImGuiCol_Text
                                           : ImGuiCol_TextDisabled),
          state.Label.c_str());
      ImGui::GetWindowDrawList()->PopClipRect();
      ImGui::SetCursorScreenPos(contentCursor);
      ImGui::Dummy(ImVec2(0.0F, 0.0F));
      ImGui::SetCursorScreenPos(contentCursor);
    }

    if (hasHeaderRight) {
      const float controlWidth = Ui(state.HeaderRightWidth);
      const float controlHeight = ImGui::GetFrameHeight();
      ImGui::SetCursorScreenPos(ImVec2(
          std::max(headerMinimum.x, headerMaximum.x - controlWidth),
          headerMinimum.y
              + std::max((headerMaximum.y - headerMinimum.y - controlHeight)
                             * 0.5F,
                  0.0F)));
      ImGui::PushID(state.Id.empty() ? state.Label.c_str()
                                     : state.Id.c_str());
      state.HeaderRight.Render();
      ImGui::PopID();
      ImGui::SetCursorScreenPos(contentCursor);
      ImGui::Dummy(ImVec2(0.0F, 0.0F));
      ImGui::SetCursorScreenPos(contentCursor);
    }

    if (isOpen) {
      for (const UIElement &childElement : children) {
        childElement.Render();
      }

      if (ShouldTreePop(state.Flags)) {
        ImGui::TreePop();
      }
    }

    if (state.Open != nullptr) {
      *state.Open = isOpen;
    }
  });
}

FoldOutBuilder FoldOut(std::string label) {
  return FoldOutBuilder(std::move(label));
}
} // namespace FlightUI
