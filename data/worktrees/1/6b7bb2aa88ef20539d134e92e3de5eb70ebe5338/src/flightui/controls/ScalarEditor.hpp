#pragma once

#include "flightui/core/UIElement.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace FlightUI {
using ScalarEditorChangedAction = std::function<void(double)>;

std::optional<double> NormalizeScalarEditorValue(double value, double minimum,
    double maximum);

class ScalarEditorBuilder {
public:
  // Lifetime
  ScalarEditorBuilder(std::string id, double value);
  ScalarEditorBuilder(const ScalarEditorBuilder &other);
  ScalarEditorBuilder(ScalarEditorBuilder &&other) noexcept;
  ScalarEditorBuilder &operator=(const ScalarEditorBuilder &other);
  ScalarEditorBuilder &operator=(ScalarEditorBuilder &&other) noexcept;
  ~ScalarEditorBuilder();

  // Explicit configuration
  ScalarEditorBuilder &SetRange(double minimum, double maximum);
  ScalarEditorBuilder &SetStep(double step);
  ScalarEditorBuilder &SetFastStep(double step);
  ScalarEditorBuilder &SetFormat(std::string format);
  ScalarEditorBuilder &SetWidth(float width);
  ScalarEditorBuilder &SetInputWidth(float width);
  ScalarEditorBuilder &SetTrailingWidth(float width);
  ScalarEditorBuilder &SetShowSlider(bool show);
  ScalarEditorBuilder &SetShowInput(bool show);
  ScalarEditorBuilder &SetShowStepper(bool show);
  ScalarEditorBuilder &SetEnabled(bool enabled);
  ScalarEditorBuilder &SetTooltip(std::string tooltip);
  ScalarEditorBuilder &SetOnChanged(ScalarEditorChangedAction onChanged);

  // Fluent configuration
  ScalarEditorBuilder &Range(double minimum, double maximum);
  ScalarEditorBuilder &Step(double step);
  ScalarEditorBuilder &FastStep(double step);
  ScalarEditorBuilder &Format(std::string format);
  ScalarEditorBuilder &Width(float width);
  ScalarEditorBuilder &InputWidth(float width);
  ScalarEditorBuilder &TrailingWidth(float width);
  ScalarEditorBuilder &ShowSlider(bool show = true);
  ScalarEditorBuilder &ShowInput(bool show = true);
  ScalarEditorBuilder &ShowStepper(bool show = true);
  ScalarEditorBuilder &Enabled(bool enabled);
  ScalarEditorBuilder &Tooltip(std::string tooltip);
  ScalarEditorBuilder &OnChanged(ScalarEditorChangedAction onChanged);

  // Element conversion
  operator UIElement() const;

private:
  // Implementation state
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};

ScalarEditorBuilder ScalarEditor(std::string id, double value);
} // namespace FlightUI
