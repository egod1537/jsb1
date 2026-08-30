#include "flightui/layout/ToggleFoldOut.hpp"

#include "flightui/controls/Toggle.hpp"

#include <utility>

namespace FlightUI {
namespace {
constexpr float HeaderToggleWidth = 18.0F;
}

class ToggleFoldOutBuilder::Impl {
public:
  std::string Label;
  bool Value = false;
  bool *Open = nullptr;
  bool DefaultOpen = false;
  FoldOutVariant Variant = FoldOutVariant::Default;
  bool ToggleEnabled = true;
  bool Visible = true;
  std::string Tooltip;
  std::string Id;
  ToggleFoldOutChangedAction OnChanged;
};

ToggleFoldOutBuilder::ToggleFoldOutBuilder(std::string label, bool value)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Label = std::move(label);
  m_Impl->Value = value;
}

ToggleFoldOutBuilder::ToggleFoldOutBuilder(const ToggleFoldOutBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

ToggleFoldOutBuilder::ToggleFoldOutBuilder(
    ToggleFoldOutBuilder &&other) noexcept = default;

ToggleFoldOutBuilder &ToggleFoldOutBuilder::operator=(
    const ToggleFoldOutBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }
  return *this;
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::operator=(
    ToggleFoldOutBuilder &&other) noexcept = default;

ToggleFoldOutBuilder::~ToggleFoldOutBuilder() = default;

ToggleFoldOutBuilder &ToggleFoldOutBuilder::SetOpen(bool &isOpen) {
  m_Impl->Open = &isOpen;
  return *this;
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::SetDefaultOpen(bool open) {
  m_Impl->DefaultOpen = open;
  return *this;
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::SetVariant(FoldOutVariant variant) {
  m_Impl->Variant = variant;
  return *this;
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::SetToggleEnabled(bool enabled) {
  m_Impl->ToggleEnabled = enabled;
  return *this;
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::SetVisible(bool visible) {
  m_Impl->Visible = visible;
  return *this;
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::SetId(std::string id) {
  m_Impl->Id = std::move(id);
  return *this;
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::SetOnChanged(
    ToggleFoldOutChangedAction onChanged) {
  m_Impl->OnChanged = std::move(onChanged);
  return *this;
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::Open(bool &isOpen) {
  return SetOpen(isOpen);
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::DefaultOpen(bool open) {
  return SetDefaultOpen(open);
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::Variant(FoldOutVariant variant) {
  return SetVariant(variant);
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::Section() {
  return SetVariant(FoldOutVariant::Section);
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::ToggleEnabled(bool enabled) {
  return SetToggleEnabled(enabled);
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::Visible(bool visible) {
  return SetVisible(visible);
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::Id(std::string id) {
  return SetId(std::move(id));
}

ToggleFoldOutBuilder &ToggleFoldOutBuilder::OnChanged(
    ToggleFoldOutChangedAction onChanged) {
  return SetOnChanged(std::move(onChanged));
}

UIElement ToggleFoldOutBuilder::operator[](UIElement child) const {
  Children children;
  children.push_back(std::move(child));
  return (*this)[std::move(children)];
}

UIElement ToggleFoldOutBuilder::operator[](ChildrenBuilder children) const {
  return (*this)[std::move(children).TakeChildren()];
}

UIElement ToggleFoldOutBuilder::operator[](Children children) const {
  Impl state = *m_Impl;
  FoldOutBuilder foldOut = FoldOut(state.Label)
                               .Framed()
                               .SpanAvailWidth()
                               .Visible(state.Visible)
                               .Tooltip(state.Tooltip)
                               .Id(state.Id)
                               .Variant(state.Variant)
                               .HeaderLeft(Toggle("##Enabled", state.Value)
                                               .Enabled(state.ToggleEnabled)
                                               .Id(state.Id + "Toggle")
                                               .OnChanged(state.OnChanged),
                                   HeaderToggleWidth);
  if (state.Open != nullptr) {
    foldOut.Open(*state.Open);
  } else {
    foldOut.DefaultOpen(state.DefaultOpen);
  }
  return foldOut[std::move(children)];
}

ToggleFoldOutBuilder ToggleFoldOut(std::string label, bool value) {
  return ToggleFoldOutBuilder(std::move(label), value);
}
} // namespace FlightUI
