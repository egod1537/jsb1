#include "flightui/controls/ScalarEditor.hpp"

#include "flightui/controls/Input.hpp"
#include "flightui/controls/Slider.hpp"
#include "flightui/layout/HorizontalLayout.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace FlightUI {
namespace {
constexpr float DefaultInputWidth = 88.0F;
constexpr float DefaultSpacing = 6.0F;
} // namespace

class ScalarEditorBuilder::Impl {
public:
  std::string Id;
  double Value = 0.0;
  double Minimum = 0.0;
  double Maximum = 1.0;
  double Step = 0.01;
  double FastStep = 0.1;
  std::string Format = "%.3f";
  float Width = 0.0F;
  float InputWidth = DefaultInputWidth;
  float TrailingWidth = 0.0F;
  bool ShowSlider = true;
  bool HasRange = false;
  bool ShowInput = true;
  bool ShowStepper = true;
  bool Enabled = true;
  std::string Tooltip;
  ScalarEditorChangedAction OnChanged;
};

std::optional<double> NormalizeScalarEditorValue(double value, double minimum,
    double maximum) {
  if (!std::isfinite(value) || !std::isfinite(minimum)
      || !std::isfinite(maximum)) {
    return std::nullopt;
  }
  if (minimum > maximum) {
    std::swap(minimum, maximum);
  }
  return std::clamp(value, minimum, maximum);
}

ScalarEditorBuilder::ScalarEditorBuilder(std::string id, double value)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Id = std::move(id);
  m_Impl->Value = value;
}

ScalarEditorBuilder::ScalarEditorBuilder(const ScalarEditorBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

ScalarEditorBuilder::ScalarEditorBuilder(
    ScalarEditorBuilder &&other) noexcept = default;

ScalarEditorBuilder &ScalarEditorBuilder::operator=(
    const ScalarEditorBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }
  return *this;
}

ScalarEditorBuilder &ScalarEditorBuilder::operator=(
    ScalarEditorBuilder &&other) noexcept = default;

ScalarEditorBuilder::~ScalarEditorBuilder() = default;

ScalarEditorBuilder &ScalarEditorBuilder::SetRange(double minimum,
    double maximum) {
  m_Impl->Minimum = std::min(minimum, maximum);
  m_Impl->Maximum = std::max(minimum, maximum);
  m_Impl->HasRange = true;
  return *this;
}

ScalarEditorBuilder &ScalarEditorBuilder::SetStep(double step) {
  m_Impl->Step = std::max(0.0, step);
  return *this;
}

ScalarEditorBuilder &ScalarEditorBuilder::SetFastStep(double step) {
  m_Impl->FastStep = std::max(0.0, step);
  return *this;
}

ScalarEditorBuilder &ScalarEditorBuilder::SetFormat(std::string format) {
  m_Impl->Format = std::move(format);
  return *this;
}

ScalarEditorBuilder &ScalarEditorBuilder::SetWidth(float width) {
  m_Impl->Width = std::max(0.0F, width);
  return *this;
}

ScalarEditorBuilder &ScalarEditorBuilder::SetInputWidth(float width) {
  m_Impl->InputWidth = std::max(0.0F, width);
  return *this;
}

ScalarEditorBuilder &ScalarEditorBuilder::SetTrailingWidth(float width) {
  m_Impl->TrailingWidth = std::max(0.0F, width);
  return *this;
}

ScalarEditorBuilder &ScalarEditorBuilder::SetShowSlider(bool show) {
  m_Impl->ShowSlider = show;
  return *this;
}

ScalarEditorBuilder &ScalarEditorBuilder::SetShowInput(bool show) {
  m_Impl->ShowInput = show;
  return *this;
}

ScalarEditorBuilder &ScalarEditorBuilder::SetShowStepper(bool show) {
  m_Impl->ShowStepper = show;
  return *this;
}

ScalarEditorBuilder &ScalarEditorBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

ScalarEditorBuilder &ScalarEditorBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

ScalarEditorBuilder &ScalarEditorBuilder::SetOnChanged(
    ScalarEditorChangedAction onChanged) {
  m_Impl->OnChanged = std::move(onChanged);
  return *this;
}

ScalarEditorBuilder &ScalarEditorBuilder::Range(double minimum,
    double maximum) {
  return SetRange(minimum, maximum);
}

ScalarEditorBuilder &ScalarEditorBuilder::Step(double step) {
  return SetStep(step);
}

ScalarEditorBuilder &ScalarEditorBuilder::FastStep(double step) {
  return SetFastStep(step);
}

ScalarEditorBuilder &ScalarEditorBuilder::Format(std::string format) {
  return SetFormat(std::move(format));
}

ScalarEditorBuilder &ScalarEditorBuilder::Width(float width) {
  return SetWidth(width);
}

ScalarEditorBuilder &ScalarEditorBuilder::InputWidth(float width) {
  return SetInputWidth(width);
}

ScalarEditorBuilder &ScalarEditorBuilder::TrailingWidth(float width) {
  return SetTrailingWidth(width);
}

ScalarEditorBuilder &ScalarEditorBuilder::ShowSlider(bool show) {
  return SetShowSlider(show);
}

ScalarEditorBuilder &ScalarEditorBuilder::ShowInput(bool show) {
  return SetShowInput(show);
}

ScalarEditorBuilder &ScalarEditorBuilder::ShowStepper(bool show) {
  return SetShowStepper(show);
}

ScalarEditorBuilder &ScalarEditorBuilder::Enabled(bool enabled) {
  return SetEnabled(enabled);
}

ScalarEditorBuilder &ScalarEditorBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

ScalarEditorBuilder &ScalarEditorBuilder::OnChanged(
    ScalarEditorChangedAction onChanged) {
  return SetOnChanged(std::move(onChanged));
}

ScalarEditorBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  const auto onChanged = [state](double value) {
    const std::optional<double> normalized =
        state.HasRange
            ? NormalizeScalarEditorValue(value, state.Minimum, state.Maximum)
        : std::isfinite(value) ? std::optional(value)
                               : std::nullopt;
    if (normalized.has_value() && state.OnChanged) {
      state.OnChanged(*normalized);
    }
  };

  HorizontalLayoutBuilder layout = HorizontalLayout().Spacing(DefaultSpacing);
  if (state.ShowSlider && state.HasRange) {
    SliderDoubleBuilder slider =
        SliderDouble("##Slider", state.Value, state.Minimum, state.Maximum)
            .Format(state.Format)
            .Enabled(state.Enabled)
            .Tooltip(state.Tooltip)
            .Id(state.Id + "Slider")
            .OnChanged(onChanged);
    if (state.Width > 0.0F) {
      slider.Width(state.Width);
    } else {
      const float inputReservation =
          state.ShowInput ? state.InputWidth + DefaultSpacing : 0.0F;
      slider.FillAvailableWidth(inputReservation + state.TrailingWidth);
    }
    layout = layout + slider;
  }

  if (state.ShowInput) {
    InputDoubleBuilder input = InputDouble("##Value", state.Value)
                                   .Width(state.InputWidth)
                                   .Format(state.Format)
                                   .Enabled(state.Enabled)
                                   .Tooltip(state.Tooltip)
                                   .Id(state.Id + "Input")
                                   .OnChanged(onChanged);
    if (state.ShowStepper) {
      input.Step(state.Step).FastStep(state.FastStep);
    }
    layout = layout + input;
  }

  return layout;
}

ScalarEditorBuilder ScalarEditor(std::string id, double value) {
  return ScalarEditorBuilder(std::move(id), value);
}
} // namespace FlightUI
