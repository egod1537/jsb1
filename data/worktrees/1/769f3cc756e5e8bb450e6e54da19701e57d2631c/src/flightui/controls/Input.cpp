#include "flightui/controls/Input.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIRenderHelpers.hpp"

#include <imgui.h>

#include <utility>

namespace FlightUI {
class InputDoubleBuilder::Impl {
public:
  std::string Label;
  double Value = 0.0;
  double Step = 0.0;
  double FastStep = 0.0;
  InputDoubleChangedAction OnChanged;
  std::string Format = "%.3f";
  float Width = 0.0F;
  bool Enabled = true;
  std::string Tooltip;
  std::string Id;
};

InputDoubleBuilder::InputDoubleBuilder(std::string label, double value)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Label = std::move(label);
  m_Impl->Value = value;
}

InputDoubleBuilder::InputDoubleBuilder(const InputDoubleBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

InputDoubleBuilder::InputDoubleBuilder(
    InputDoubleBuilder &&other) noexcept = default;

InputDoubleBuilder &InputDoubleBuilder::operator=(
    const InputDoubleBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

InputDoubleBuilder &InputDoubleBuilder::operator=(
    InputDoubleBuilder &&other) noexcept = default;

InputDoubleBuilder::~InputDoubleBuilder() = default;

InputDoubleBuilder &InputDoubleBuilder::SetOnChanged(
    InputDoubleChangedAction onChanged) {
  m_Impl->OnChanged = std::move(onChanged);
  return *this;
}

InputDoubleBuilder &InputDoubleBuilder::SetStep(double step) {
  m_Impl->Step = step;
  return *this;
}

InputDoubleBuilder &InputDoubleBuilder::SetFastStep(double step) {
  m_Impl->FastStep = step;
  return *this;
}

InputDoubleBuilder &InputDoubleBuilder::SetFormat(std::string format) {
  m_Impl->Format = std::move(format);
  return *this;
}

InputDoubleBuilder &InputDoubleBuilder::SetWidth(float width) {
  m_Impl->Width = width;
  return *this;
}

InputDoubleBuilder &InputDoubleBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

InputDoubleBuilder &InputDoubleBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

InputDoubleBuilder &InputDoubleBuilder::SetId(std::string id) {
  m_Impl->Id = std::move(id);
  return *this;
}

InputDoubleBuilder &InputDoubleBuilder::OnChanged(
    InputDoubleChangedAction onChanged) {
  return SetOnChanged(std::move(onChanged));
}

InputDoubleBuilder &InputDoubleBuilder::Step(double step) {
  return SetStep(step);
}

InputDoubleBuilder &InputDoubleBuilder::FastStep(double step) {
  return SetFastStep(step);
}

InputDoubleBuilder &InputDoubleBuilder::Format(std::string format) {
  return SetFormat(std::move(format));
}

InputDoubleBuilder &InputDoubleBuilder::Width(float width) {
  return SetWidth(width);
}

InputDoubleBuilder &InputDoubleBuilder::Enabled(bool enabled) {
  return SetEnabled(enabled);
}

InputDoubleBuilder &InputDoubleBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

InputDoubleBuilder &InputDoubleBuilder::Id(std::string id) {
  return SetId(std::move(id));
}

InputDoubleBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  return CreateElement([state] {
    Internal::IdScope idScope(state.Id);
    Internal::DisabledScope disabledScope(!state.Enabled);
    Internal::ItemWidthScope widthScope(state.Width);
    double value = state.Value;
    if (ImGui::InputDouble(state.Label.c_str(),
            &value,
            state.Step,
            state.FastStep,
            state.Format.c_str())
        && state.OnChanged) {
      state.OnChanged(value);
    }
    Internal::ShowTooltipIfHovered(state.Tooltip);
  });
}

InputDoubleBuilder InputDouble(std::string label, double value) {
  return InputDoubleBuilder(std::move(label), value);
}
} // namespace FlightUI
