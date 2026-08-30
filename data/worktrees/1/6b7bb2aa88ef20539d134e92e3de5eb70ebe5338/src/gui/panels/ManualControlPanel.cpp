#include "gui/panels/ManualControlPanel.hpp"

#include "flightui/FlightUI.hpp"

#include <cmath>

namespace gui {
namespace UI = FlightUI;

namespace {
constexpr double ManualInputStep = 0.05;
constexpr float ManualInputLayoutSpacing = 6.0F;
constexpr float ManualInputRowSpacing = 8.0F;
constexpr float ManualInputButtonWidth = 32.0F;

bool WasShortcutPressed(UI::Key key) { return UI::IsKeyPressed(key, true); }

bool CanApplyManualInputShortcuts() {
  return UI::IsCurrentWindowFocused() && !UI::WantsTextInput();
}

bool IsManualControlAllowed(const AutopilotPanelState &autopilotState,
    control::ControlAxis axis) {
  switch (axis) {
  case control::ControlAxis::Elevator:
    return true;
  case control::ControlAxis::Aileron:
    return !autopilotState.rollHold;
  case control::ControlAxis::Rudder:
    return true;
  case control::ControlAxis::Throttle:
    return true;
  }

  return true;
}

const char *ManualControlLockTooltip(const AutopilotPanelState &autopilotState,
    control::ControlAxis axis) {
  switch (axis) {
  case control::ControlAxis::Elevator:
    break;
  case control::ControlAxis::Aileron:
    if (autopilotState.rollHold) {
      return "Roll Hold is controlling aileron.";
    }
    break;
  case control::ControlAxis::Rudder:
    break;
  case control::ControlAxis::Throttle:
    break;
  }

  return "";
}

void AdjustManualInput(ManualControlPanelProps &props,
    control::ControlAxis axis, double delta) {
  if (!IsManualControlAllowed(props.autopilotState, axis)) {
    return;
  }

  control::ControlInput input = props.input;
  if (control::AdjustControlAxisValue(input, axis, delta)
      && props.events.IsConnected()) {
    props.events.Emit({input});
    props.input = input;
  }
}

void SetManualInput(ManualControlPanelProps &props, control::ControlAxis axis,
    double value) {
  if (!IsManualControlAllowed(props.autopilotState, axis)
      || !std::isfinite(value)) {
    return;
  }

  control::ControlInput input = props.input;
  if (control::SetControlAxisValue(input, axis, value)
      && props.events.IsConnected()) {
    props.events.Emit({input});
    props.input = input;
  }
}

void ApplyManualInputShortcuts(ManualControlPanelProps &props) {
  if (!CanApplyManualInputShortcuts()) {
    return;
  }

  if (WasShortcutPressed(UI::Key::F)) {
    AdjustManualInput(props, control::ControlAxis::Throttle, -ManualInputStep);
  }
  if (WasShortcutPressed(UI::Key::R)) {
    AdjustManualInput(props, control::ControlAxis::Throttle, ManualInputStep);
  }
  if (WasShortcutPressed(UI::Key::W)) {
    AdjustManualInput(props, control::ControlAxis::Elevator, -ManualInputStep);
  }
  if (WasShortcutPressed(UI::Key::S)) {
    AdjustManualInput(props, control::ControlAxis::Elevator, ManualInputStep);
  }
  if (WasShortcutPressed(UI::Key::A)) {
    AdjustManualInput(props, control::ControlAxis::Aileron, -ManualInputStep);
  }
  if (WasShortcutPressed(UI::Key::D)) {
    AdjustManualInput(props, control::ControlAxis::Aileron, ManualInputStep);
  }
  if (WasShortcutPressed(UI::Key::Q)) {
    AdjustManualInput(props, control::ControlAxis::Rudder, -ManualInputStep);
  }
  if (WasShortcutPressed(UI::Key::E)) {
    AdjustManualInput(props, control::ControlAxis::Rudder, ManualInputStep);
  }
}

UI::UIElement MakeManualScalarEditor(const char *id, double value,
    double minimum, double maximum, ManualControlPanelProps &props,
    control::ControlAxis axis, bool enabled, const char *tooltip) {
  return UI::ScalarEditor(id, value)
      .Range(minimum, maximum)
      .Step(0.01)
      .FastStep(0.1)
      .Format("%.3f")
      .TrailingWidth(ManualInputButtonWidth + ManualInputRowSpacing)
      .Enabled(enabled)
      .Tooltip(tooltip)
      .OnChanged([&props, axis](
                     double changed) { SetManualInput(props, axis, changed); });
}

UI::UIElement MakeThrottleRow(ManualControlPanelProps &props) {
  const bool enabled = IsManualControlAllowed(props.autopilotState,
      control::ControlAxis::Throttle);
  const char *tooltip = ManualControlLockTooltip(props.autopilotState,
      control::ControlAxis::Throttle);

  // clang-format off
  return UI::HorizontalLayout()
      .Spacing(ManualInputRowSpacing)
      [
        +UI::Text("Throttle")
        + UI::Button("F")
              .Enabled(enabled)
              .Tooltip(tooltip)
              .OnAction([&props] {
                AdjustManualInput(
                    props, control::ControlAxis::Throttle, -ManualInputStep);
              })
              .Width(ManualInputButtonWidth)
        + MakeManualScalarEditor("ThrottleInput",
              props.input.throttle,
              0.0,
              1.0,
              props,
              control::ControlAxis::Throttle,
              enabled,
              tooltip)
        + UI::Button("R")
              .Enabled(enabled)
              .Tooltip(tooltip)
              .OnAction([&props] {
                AdjustManualInput(
                    props, control::ControlAxis::Throttle, ManualInputStep);
              })
              .Width(ManualInputButtonWidth)
      ];
  // clang-format on
}

UI::UIElement MakeElevatorRow(ManualControlPanelProps &props) {
  const bool enabled = IsManualControlAllowed(props.autopilotState,
      control::ControlAxis::Elevator);
  const char *tooltip = ManualControlLockTooltip(props.autopilotState,
      control::ControlAxis::Elevator);

  // clang-format off
  return UI::HorizontalLayout()
      .Spacing(ManualInputRowSpacing)
      [
        +UI::Text("Elevator")
        + UI::Button("W")
              .Enabled(enabled)
              .Tooltip(tooltip)
              .OnAction([&props] {
                AdjustManualInput(
                    props, control::ControlAxis::Elevator, -ManualInputStep);
              })
              .Width(ManualInputButtonWidth)
        + MakeManualScalarEditor("ElevatorInput",
              props.input.elevator,
              -1.0,
              1.0,
              props,
              control::ControlAxis::Elevator,
              enabled,
              tooltip)
        + UI::Button("S")
              .Enabled(enabled)
              .Tooltip(tooltip)
              .OnAction([&props] {
                AdjustManualInput(
                    props, control::ControlAxis::Elevator, ManualInputStep);
              })
              .Width(ManualInputButtonWidth)
      ];
  // clang-format on
}

UI::UIElement MakeAileronRow(ManualControlPanelProps &props) {
  const bool enabled = IsManualControlAllowed(props.autopilotState,
      control::ControlAxis::Aileron);
  const char *tooltip = ManualControlLockTooltip(props.autopilotState,
      control::ControlAxis::Aileron);

  // clang-format off
  return UI::HorizontalLayout()
      .Spacing(ManualInputRowSpacing)
      [
        +UI::Text("Aileron")
        + UI::Button("A")
              .Enabled(enabled)
              .Tooltip(tooltip)
              .OnAction([&props] {
                AdjustManualInput(
                    props, control::ControlAxis::Aileron, -ManualInputStep);
              })
              .Width(ManualInputButtonWidth)
        + MakeManualScalarEditor("AileronInput",
              props.input.aileron,
              -1.0,
              1.0,
              props,
              control::ControlAxis::Aileron,
              enabled,
              tooltip)
        + UI::Button("D")
              .Enabled(enabled)
              .Tooltip(tooltip)
              .OnAction([&props] {
                AdjustManualInput(
                    props, control::ControlAxis::Aileron, ManualInputStep);
              })
              .Width(ManualInputButtonWidth)
      ];
  // clang-format on
}

UI::UIElement MakeRudderRow(ManualControlPanelProps &props) {
  const bool enabled = IsManualControlAllowed(props.autopilotState,
      control::ControlAxis::Rudder);
  const char *tooltip = ManualControlLockTooltip(props.autopilotState,
      control::ControlAxis::Rudder);

  // clang-format off
  return UI::HorizontalLayout()
      .Spacing(ManualInputRowSpacing)
      [
        +UI::Text("Rudder")
        + UI::Button("Q")
              .Enabled(enabled)
              .Tooltip(tooltip)
              .OnAction([&props] {
                AdjustManualInput(
                    props, control::ControlAxis::Rudder, -ManualInputStep);
              })
              .Width(ManualInputButtonWidth)
        + MakeManualScalarEditor("RudderInput",
              props.input.rudder,
              -1.0,
              1.0,
              props,
              control::ControlAxis::Rudder,
              enabled,
              tooltip)
        + UI::Button("E")
              .Enabled(enabled)
              .Tooltip(tooltip)
              .OnAction([&props] {
                AdjustManualInput(
                    props, control::ControlAxis::Rudder, ManualInputStep);
              })
              .Width(ManualInputButtonWidth)
      ];
  // clang-format on
}

UI::UIElement MakeManualInputLayout(ManualControlPanelProps &props) {
  // clang-format off
  return UI::VerticalLayout()
      .Spacing(ManualInputLayoutSpacing)
      [
        +UI::Text("Control Inputs")
        + MakeThrottleRow(props)
        + MakeElevatorRow(props)
        + MakeAileronRow(props)
        + MakeRudderRow(props)
        + UI::ValueLabel("Pitch Trim", props.pitchTrim, "%.3f")
      ];
  // clang-format on
}
} // namespace

void ManualControlPanel::Draw(const ManualControlPanelProps &props) {
  ManualControlPanelProps editableProps = props;
  ApplyManualInputShortcuts(editableProps);
  MakeManualInputLayout(editableProps).Render();
}
} // namespace gui
