#include "flightui/layout/HorizontalLayout.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIScale.hpp"

#include <imgui.h>

#include <utility>

namespace FlightUI {
class HorizontalLayoutBuilder::Impl {
public:
  Children ChildrenList;
  float Spacing = 0.0F;
  bool HasSpacing = false;
};

HorizontalLayoutBuilder::HorizontalLayoutBuilder()
    : m_Impl(std::make_unique<Impl>()) {}

HorizontalLayoutBuilder::HorizontalLayoutBuilder(Children children)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->ChildrenList = std::move(children);
}

HorizontalLayoutBuilder::HorizontalLayoutBuilder(
    const HorizontalLayoutBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

HorizontalLayoutBuilder::HorizontalLayoutBuilder(
    HorizontalLayoutBuilder &&other) noexcept = default;

HorizontalLayoutBuilder &HorizontalLayoutBuilder::operator=(
    const HorizontalLayoutBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

HorizontalLayoutBuilder &HorizontalLayoutBuilder::operator=(
    HorizontalLayoutBuilder &&other) noexcept = default;

HorizontalLayoutBuilder::~HorizontalLayoutBuilder() = default;

HorizontalLayoutBuilder &HorizontalLayoutBuilder::SetSpacing(float spacing) {
  m_Impl->Spacing = spacing;
  m_Impl->HasSpacing = true;
  return *this;
}

HorizontalLayoutBuilder &HorizontalLayoutBuilder::Spacing(float spacing) {
  return SetSpacing(spacing);
}

HorizontalLayoutBuilder HorizontalLayoutBuilder::operator+(
    UIElement child) const {
  HorizontalLayoutBuilder builder(*this);
  builder.m_Impl->ChildrenList.push_back(std::move(child));
  return builder;
}

UIElement HorizontalLayoutBuilder::operator[](UIElement child) const {
  Children children;
  children.push_back(std::move(child));
  return (*this)[std::move(children)];
}

UIElement HorizontalLayoutBuilder::operator[](ChildrenBuilder children) const {
  return (*this)[std::move(children).TakeChildren()];
}

UIElement HorizontalLayoutBuilder::operator[](Children children) const {
  HorizontalLayoutBuilder builder(*this);
  builder.m_Impl->ChildrenList.insert(builder.m_Impl->ChildrenList.end(),
      children.begin(),
      children.end());
  return builder;
}

HorizontalLayoutBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  return CreateElement([state] {
    for (std::size_t index = 0; index < state.ChildrenList.size(); ++index) {
      if (index > 0) {
        if (state.HasSpacing) {
          ImGui::SameLine(0.0F, Ui(state.Spacing));
        } else {
          ImGui::SameLine();
        }
      }

      state.ChildrenList[index].Render();
    }
  });
}

HorizontalLayoutBuilder HorizontalLayout() { return HorizontalLayoutBuilder(); }

HorizontalLayoutBuilder HorizontalLayout(Children children) {
  return HorizontalLayoutBuilder(std::move(children));
}
} // namespace FlightUI
