#include "flightui/core/UIElement.hpp"
#include "flightui/core/UIElementFactory.hpp"

#include <memory>
#include <utility>

namespace FlightUI {
class UIElement::Impl {
public:
  virtual ~Impl() = default;
  virtual void Render() const = 0;
};

UIElement::UIElement() = default;

UIElement::UIElement(const UIElement &other) = default;

UIElement::UIElement(UIElement &&other) noexcept = default;

UIElement &UIElement::operator=(const UIElement &other) = default;

UIElement &UIElement::operator=(UIElement &&other) noexcept = default;

UIElement::~UIElement() = default;

void UIElement::Render() const {
  if (m_Impl == nullptr) {
    return;
  }

  m_Impl->Render();
}

bool UIElement::IsValid() const { return m_Impl != nullptr; }

UIElement::UIElement(std::shared_ptr<Impl> impl) : m_Impl(std::move(impl)) {}

ChildrenBuilder::ChildrenBuilder() = default;

ChildrenBuilder::ChildrenBuilder(UIElement child) {
  children_.push_back(std::move(child));
}

ChildrenBuilder ChildrenBuilder::operator+(UIElement child) const {
  ChildrenBuilder builder(*this);
  builder.children_.push_back(std::move(child));
  return builder;
}

const Children &ChildrenBuilder::GetChildren() const { return children_; }

Children ChildrenBuilder::TakeChildren() && { return std::move(children_); }

ChildrenBuilder operator+(UIElement child) {
  return ChildrenBuilder(std::move(child));
}

UIElement CreateElement(Action renderAction) {
  class ActionElementImpl final : public UIElement::Impl {
  public:
    explicit ActionElementImpl(Action action)
        : m_RenderAction(std::move(action)) {}

    void Render() const override {
      if (m_RenderAction) {
        m_RenderAction();
      }
    }

  private:
    Action m_RenderAction;
  };

  return UIElement(
      std::make_shared<ActionElementImpl>(std::move(renderAction)));
}
} // namespace FlightUI
