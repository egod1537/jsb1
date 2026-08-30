#include "flightui/layout/VerticalLayout.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIScale.hpp"

#include <imgui.h>

#include <utility>

namespace FlightUI {
class VerticalLayoutBuilder::Impl {
public:
  Children ChildrenList;
  float Spacing = 0.0F;
  bool HasSpacing = false;
};

VerticalLayoutBuilder::VerticalLayoutBuilder()
    : m_Impl(std::make_unique<Impl>()) {}

VerticalLayoutBuilder::VerticalLayoutBuilder(Children children)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->ChildrenList = std::move(children);
}

VerticalLayoutBuilder::VerticalLayoutBuilder(const VerticalLayoutBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

VerticalLayoutBuilder::VerticalLayoutBuilder(
    VerticalLayoutBuilder &&other) noexcept = default;

VerticalLayoutBuilder &VerticalLayoutBuilder::operator=(
    const VerticalLayoutBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

VerticalLayoutBuilder &VerticalLayoutBuilder::operator=(
    VerticalLayoutBuilder &&other) noexcept = default;

VerticalLayoutBuilder::~VerticalLayoutBuilder() = default;

VerticalLayoutBuilder &VerticalLayoutBuilder::SetSpacing(float spacing) {
  m_Impl->Spacing = spacing;
  m_Impl->HasSpacing = true;
  return *this;
}

VerticalLayoutBuilder &VerticalLayoutBuilder::Spacing(float spacing) {
  return SetSpacing(spacing);
}

VerticalLayoutBuilder VerticalLayoutBuilder::operator+(UIElement child) const {
  VerticalLayoutBuilder builder(*this);
  builder.m_Impl->ChildrenList.push_back(std::move(child));
  return builder;
}

UIElement VerticalLayoutBuilder::operator[](UIElement child) const {
  Children children;
  children.push_back(std::move(child));
  return (*this)[std::move(children)];
}

UIElement VerticalLayoutBuilder::operator[](ChildrenBuilder children) const {
  return (*this)[std::move(children).TakeChildren()];
}

UIElement VerticalLayoutBuilder::operator[](Children children) const {
  VerticalLayoutBuilder builder(*this);
  builder.m_Impl->ChildrenList.insert(builder.m_Impl->ChildrenList.end(),
      children.begin(),
      children.end());
  return builder;
}

VerticalLayoutBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  return CreateElement([state] {
    bool pushedSpacing = false;
    ImVec2 previousSpacing;

    if (state.HasSpacing) {
      previousSpacing = ImGui::GetStyle().ItemSpacing;
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
          ImVec2(previousSpacing.x, Ui(state.Spacing)));
      pushedSpacing = true;
    }

    for (const UIElement &childElement : state.ChildrenList) {
      childElement.Render();
    }

    if (pushedSpacing) {
      ImGui::PopStyleVar();
    }
  });
}

VerticalLayoutBuilder VerticalLayout() { return VerticalLayoutBuilder(); }

VerticalLayoutBuilder VerticalLayout(Children children) {
  return VerticalLayoutBuilder(std::move(children));
}
} // namespace FlightUI
