#include "flightui/controls/Slider.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIRenderHelpers.hpp"
#include "flightui/core/UIScale.hpp"

#include <algorithm>
#include <imgui.h>

#include <utility>

namespace FlightUI {
class SliderFloatBuilder::Impl {
public:
  std::string Label;
  float Value = 0.0F;
  float Minimum = 0.0F;
  float Maximum = 0.0F;
  SliderFloatChangedAction OnChanged;
  std::string Format = "%.3f";
  float Width = 0.0F;
  bool Enabled = true;
  ImGuiSliderFlags Flags = ImGuiSliderFlags_None;
  std::string Tooltip;
  std::string Id;
};

class SliderDoubleBuilder::Impl {
public:
  std::string Label;
  double Value = 0.0;
  double Minimum = 0.0;
  double Maximum = 0.0;
  SliderDoubleChangedAction OnChanged;
  std::string Format = "%.3f";
  float Width = 0.0F;
  float TrailingWidth = 0.0F;
  bool FillAvailableWidth = false;
  bool Enabled = true;
  ImGuiSliderFlags Flags = ImGuiSliderFlags_None;
  std::string Tooltip;
  std::string Id;
};

class SliderIntBuilder::Impl {
public:
  std::string Label;
  int Value = 0;
  int Minimum = 0;
  int Maximum = 0;
  SliderIntChangedAction OnChanged;
  std::string Format = "%d";
  float Width = 0.0F;
  bool Enabled = true;
  ImGuiSliderFlags Flags = ImGuiSliderFlags_None;
  std::string Tooltip;
  std::string Id;
};

SliderFloatBuilder::SliderFloatBuilder(std::string label, float value,
                                       float minimum, float maximum)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Label = std::move(label);
  m_Impl->Value = value;
  m_Impl->Minimum = minimum;
  m_Impl->Maximum = maximum;
}

SliderFloatBuilder::SliderFloatBuilder(const SliderFloatBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

SliderFloatBuilder::SliderFloatBuilder(SliderFloatBuilder &&other) noexcept =
    default;

SliderFloatBuilder &
SliderFloatBuilder::operator=(const SliderFloatBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

SliderFloatBuilder &
SliderFloatBuilder::operator=(SliderFloatBuilder &&other) noexcept = default;

SliderFloatBuilder::~SliderFloatBuilder() = default;

SliderFloatBuilder &
SliderFloatBuilder::SetOnChanged(SliderFloatChangedAction onChanged) {
  m_Impl->OnChanged = std::move(onChanged);
  return *this;
}

SliderFloatBuilder &SliderFloatBuilder::SetFormat(std::string format) {
  m_Impl->Format = std::move(format);
  return *this;
}

SliderFloatBuilder &SliderFloatBuilder::SetWidth(float width) {
  m_Impl->Width = width;
  return *this;
}

SliderFloatBuilder &SliderFloatBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

SliderFloatBuilder &SliderFloatBuilder::SetFlags(ImGuiSliderFlags flags) {
  m_Impl->Flags = flags;
  return *this;
}

SliderFloatBuilder &SliderFloatBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

SliderFloatBuilder &SliderFloatBuilder::SetId(std::string id) {
  m_Impl->Id = std::move(id);
  return *this;
}

SliderFloatBuilder &
SliderFloatBuilder::OnChanged(SliderFloatChangedAction onChanged) {
  return SetOnChanged(std::move(onChanged));
}

SliderFloatBuilder &SliderFloatBuilder::Format(std::string format) {
  return SetFormat(std::move(format));
}

SliderFloatBuilder &SliderFloatBuilder::Width(float width) {
  return SetWidth(width);
}

SliderFloatBuilder &SliderFloatBuilder::Enabled(bool enabled) {
  return SetEnabled(enabled);
}

SliderFloatBuilder &SliderFloatBuilder::Flags(ImGuiSliderFlags flags) {
  return SetFlags(flags);
}

SliderFloatBuilder &SliderFloatBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

SliderFloatBuilder &SliderFloatBuilder::Id(std::string id) {
  return SetId(std::move(id));
}

SliderFloatBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  return CreateElement([state] {
    Internal::IdScope idScope(state.Id);
    Internal::DisabledScope disabledScope(!state.Enabled);
    Internal::ItemWidthScope widthScope(state.Width);
    float value = state.Value;
    if (ImGui::SliderFloat(state.Label.c_str(), &value, state.Minimum,
                           state.Maximum, state.Format.c_str(), state.Flags) &&
        state.OnChanged) {
      state.OnChanged(value);
    }
    Internal::ShowTooltipIfHovered(state.Tooltip);
  });
}

SliderDoubleBuilder::SliderDoubleBuilder(std::string label, double value,
                                         double minimum, double maximum)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Label = std::move(label);
  m_Impl->Value = value;
  m_Impl->Minimum = minimum;
  m_Impl->Maximum = maximum;
}

SliderDoubleBuilder::SliderDoubleBuilder(const SliderDoubleBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

SliderDoubleBuilder::SliderDoubleBuilder(SliderDoubleBuilder &&other) noexcept =
    default;

SliderDoubleBuilder &
SliderDoubleBuilder::operator=(const SliderDoubleBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

SliderDoubleBuilder &
SliderDoubleBuilder::operator=(SliderDoubleBuilder &&other) noexcept = default;

SliderDoubleBuilder::~SliderDoubleBuilder() = default;

SliderDoubleBuilder &
SliderDoubleBuilder::SetOnChanged(SliderDoubleChangedAction onChanged) {
  m_Impl->OnChanged = std::move(onChanged);
  return *this;
}

SliderDoubleBuilder &SliderDoubleBuilder::SetFormat(std::string format) {
  m_Impl->Format = std::move(format);
  return *this;
}

SliderDoubleBuilder &SliderDoubleBuilder::SetWidth(float width) {
  m_Impl->Width = width;
  m_Impl->FillAvailableWidth = false;
  return *this;
}

SliderDoubleBuilder &SliderDoubleBuilder::SetFillAvailableWidth(
    float trailingWidth) {
  m_Impl->TrailingWidth = std::max(0.0F, trailingWidth);
  m_Impl->FillAvailableWidth = true;
  return *this;
}

SliderDoubleBuilder &SliderDoubleBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

SliderDoubleBuilder &SliderDoubleBuilder::SetFlags(ImGuiSliderFlags flags) {
  m_Impl->Flags = flags;
  return *this;
}

SliderDoubleBuilder &SliderDoubleBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

SliderDoubleBuilder &SliderDoubleBuilder::SetId(std::string id) {
  m_Impl->Id = std::move(id);
  return *this;
}

SliderDoubleBuilder &
SliderDoubleBuilder::OnChanged(SliderDoubleChangedAction onChanged) {
  return SetOnChanged(std::move(onChanged));
}

SliderDoubleBuilder &SliderDoubleBuilder::Format(std::string format) {
  return SetFormat(std::move(format));
}

SliderDoubleBuilder &SliderDoubleBuilder::Width(float width) {
  return SetWidth(width);
}

SliderDoubleBuilder &SliderDoubleBuilder::FillAvailableWidth(
    float trailingWidth) {
  return SetFillAvailableWidth(trailingWidth);
}

SliderDoubleBuilder &SliderDoubleBuilder::Enabled(bool enabled) {
  return SetEnabled(enabled);
}

SliderDoubleBuilder &SliderDoubleBuilder::Flags(ImGuiSliderFlags flags) {
  return SetFlags(flags);
}

SliderDoubleBuilder &SliderDoubleBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

SliderDoubleBuilder &SliderDoubleBuilder::Id(std::string id) {
  return SetId(std::move(id));
}

SliderDoubleBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  return CreateElement([state] {
    Internal::IdScope idScope(state.Id);
    Internal::DisabledScope disabledScope(!state.Enabled);
    const float width =
        state.FillAvailableWidth
            ? std::max(1.0F,
                  ImGui::GetContentRegionAvail().x - Ui(state.TrailingWidth))
            : state.Width;
    Internal::ItemWidthScope widthScope(width);
    double value = state.Value;
    if (ImGui::SliderScalar(state.Label.c_str(), ImGuiDataType_Double, &value,
                            &state.Minimum, &state.Maximum,
                            state.Format.c_str(), state.Flags) &&
        state.OnChanged) {
      state.OnChanged(value);
    }
    Internal::ShowTooltipIfHovered(state.Tooltip);
  });
}

SliderIntBuilder::SliderIntBuilder(std::string label, int value, int minimum,
                                   int maximum)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Label = std::move(label);
  m_Impl->Value = value;
  m_Impl->Minimum = minimum;
  m_Impl->Maximum = maximum;
}

SliderIntBuilder::SliderIntBuilder(const SliderIntBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

SliderIntBuilder::SliderIntBuilder(SliderIntBuilder &&other) noexcept = default;

SliderIntBuilder &SliderIntBuilder::operator=(const SliderIntBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

SliderIntBuilder &
SliderIntBuilder::operator=(SliderIntBuilder &&other) noexcept = default;

SliderIntBuilder::~SliderIntBuilder() = default;

SliderIntBuilder &
SliderIntBuilder::SetOnChanged(SliderIntChangedAction onChanged) {
  m_Impl->OnChanged = std::move(onChanged);
  return *this;
}

SliderIntBuilder &SliderIntBuilder::SetFormat(std::string format) {
  m_Impl->Format = std::move(format);
  return *this;
}

SliderIntBuilder &SliderIntBuilder::SetWidth(float width) {
  m_Impl->Width = width;
  return *this;
}

SliderIntBuilder &SliderIntBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

SliderIntBuilder &SliderIntBuilder::SetFlags(ImGuiSliderFlags flags) {
  m_Impl->Flags = flags;
  return *this;
}

SliderIntBuilder &SliderIntBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

SliderIntBuilder &SliderIntBuilder::SetId(std::string id) {
  m_Impl->Id = std::move(id);
  return *this;
}

SliderIntBuilder &
SliderIntBuilder::OnChanged(SliderIntChangedAction onChanged) {
  return SetOnChanged(std::move(onChanged));
}

SliderIntBuilder &SliderIntBuilder::Format(std::string format) {
  return SetFormat(std::move(format));
}

SliderIntBuilder &SliderIntBuilder::Width(float width) {
  return SetWidth(width);
}

SliderIntBuilder &SliderIntBuilder::Enabled(bool enabled) {
  return SetEnabled(enabled);
}

SliderIntBuilder &SliderIntBuilder::Flags(ImGuiSliderFlags flags) {
  return SetFlags(flags);
}

SliderIntBuilder &SliderIntBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

SliderIntBuilder &SliderIntBuilder::Id(std::string id) {
  return SetId(std::move(id));
}

SliderIntBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  return CreateElement([state] {
    Internal::IdScope idScope(state.Id);
    Internal::DisabledScope disabledScope(!state.Enabled);
    Internal::ItemWidthScope widthScope(state.Width);
    int value = state.Value;
    if (ImGui::SliderInt(state.Label.c_str(), &value, state.Minimum,
                         state.Maximum, state.Format.c_str(), state.Flags) &&
        state.OnChanged) {
      state.OnChanged(value);
    }
    Internal::ShowTooltipIfHovered(state.Tooltip);
  });
}

SliderFloatBuilder SliderFloat(std::string label, float value, float minimum,
                               float maximum) {
  return SliderFloatBuilder(std::move(label), value, minimum, maximum);
}

SliderDoubleBuilder SliderDouble(std::string label, double value,
                                 double minimum, double maximum) {
  return SliderDoubleBuilder(std::move(label), value, minimum, maximum);
}

SliderIntBuilder SliderInt(std::string label, int value, int minimum,
                           int maximum) {
  return SliderIntBuilder(std::move(label), value, minimum, maximum);
}
} // namespace FlightUI
