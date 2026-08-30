#include "gui/panels/BaselineAutopilotPanel.hpp"

#include "flightui/FlightUI.hpp"

#include <string>

namespace gui {
namespace UI = FlightUI;

namespace {
constexpr float AutopilotParameterLabelWidth = 112.0F;
constexpr float AutopilotTargetInputWidth = 140.0F;
constexpr float AutopilotParameterInputWidth = 88.0F;
constexpr float HoldCaptureButtonWidth = 96.0F;

UI::PropertyRowBuilder RenderPx4ParameterRow(
    const BaselinePx4RollHoldParameterBinding &binding,
    const BaselineAutopilotPanelState &state,
    architecture::EventSink<BaselineRollHoldValueChanged> events) {
  const auto &metadata =
      gnc::GetPx4RollHoldParameterMetadata(binding.parameter);
  const std::string editorId = std::string(metadata.name) + "Editor";
  const std::string tooltip = metadata.unit.empty()
                                  ? std::string(metadata.description)
                                  : std::string(metadata.description) + " ("
                                        + std::string(metadata.unit) + ")";

  return UI::PropertyRow(std::string(metadata.name))
      .Tooltip(tooltip)[UI::ScalarEditor(editorId, state.*(binding.value))
              .Range(metadata.minimum, metadata.maximum)
              .Step(metadata.increment)
              .FastStep(metadata.increment * 10.0)
              .Format("%.3f")
              .InputWidth(AutopilotParameterInputWidth)
              .Tooltip(tooltip)
              .OnChanged([events, field = binding.field](double newValue) {
                events.Emit({field, newValue});
              })];
}

UI::UIElement MakeBaselineRollHoldTuning(
    const BaselineAutopilotPanelProps &props) {
  BaselineAutopilotPanelState &state = props.state;
  UI::PropertyGridBuilder parameters =
      UI::PropertyGrid("BaselinePx4RollHoldParameters")
          .LabelWidth(AutopilotParameterLabelWidth)
          .ColumnSpacing(4.0F)
          .RowPadding(2.0F)
          .AlternatingRows();
  for (const BaselinePx4RollHoldParameterBinding &binding :
      BaselinePx4RollHoldParameterBindings) {
    parameters.Add(RenderPx4ParameterRow(binding, state, props.valueEvents));
  }

  return UI::FoldOut("PX4 Roll Hold Tuning")
      .Open(state.px4RollTuningOpen)
      .Section()
      .Id("BaselineRollHoldTuning")[UI::VerticalLayout().Spacing(
          6.0F)[+UI::TextWrapped(
                    "PX4 v1.17 Roll Hold parameters. Time constants are in "
                    "seconds; rates are in deg/s.")
                + parameters
                + UI::Button("Reset PX4 Roll Hold Tuning")
                    .OnAction(
                        [events = props.resetEvents] { events.Emit({}); })]];
}

UI::UIElement MakeBaselineRollHoldDiagnostics(
    const BaselineAutopilotPanelProps &props);

UI::UIElement MakeBaselineRollHoldSection(
    const BaselineAutopilotPanelProps &props) {
  BaselineAutopilotPanelState &state = props.state;
  UI::ToggleFoldOutBuilder foldOut =
      UI::ToggleFoldOut("Roll Hold", state.rollHold)
          .Id("BaselineRollHoldSection")
          .OnChanged([events = props.valueEvents](bool enabled) {
            events.Emit({BaselineRollHoldField::Enabled, enabled ? 1.0 : 0.0});
          })
          .DefaultOpen();

  // clang-format off
  return foldOut[
      UI::VerticalLayout()
          .Spacing(6.0F)
          [
            +UI::HorizontalLayout()
                 .Spacing(8.0F)
                 [
                   +UI::TextDisabled("Target Roll (deg)")
                   + UI::InputDouble("##BaselineRollHoldTarget",
                         state.rollTargetDeg)
                         .Width(AutopilotTargetInputWidth)
                         .Step(1.0)
                         .FastStep(10.0)
                         .Format("%.2f")
                         .OnChanged([events = props.valueEvents](double value) {
                           events.Emit(
                               {BaselineRollHoldField::TargetDeg, value});
                         })
                   + UI::Text(state.rollHold ? "Hold" : "Off")
                 ]
            + UI::HorizontalLayout()
                 .Spacing(8.0F)
                 [
                   +UI::ValueLabel("Current Roll",
                        props.currentRollDeg,
                        "%.2f deg")
                   + UI::ValueLabel("Roll Rate",
                         props.currentRollRateDegPerSec,
                         "%.2f deg/s")
                   + UI::ValueLabel("Aileron",
                         props.currentAileron,
                         "%.3f")
                   + UI::StatusBadge(props.rollHoldActive ? "Active" : "Inactive",
                         props.rollHoldActive ? UI::StatusTone::Success
                                              : UI::StatusTone::Neutral)
                   + UI::Button("Capture")
                         .Enabled(props.valueEvents.IsConnected())
                         .OnAction([events = props.valueEvents,
                                      value = props.currentRollDeg] {
                           events.Emit(
                               {BaselineRollHoldField::TargetDeg, value});
                         })
                         .Width(HoldCaptureButtonWidth)
                 ]
            + MakeBaselineRollHoldTuning(props)
            + MakeBaselineRollHoldDiagnostics(props)
          ]
      ];
  // clang-format on
}

UI::UIElement MakeBaselineRollHoldDiagnostics(
    const BaselineAutopilotPanelProps &props) {
  BaselineAutopilotPanelState &state = props.state;

  // clang-format off
  return UI::FoldOut("Diagnostics")
      .Open(state.px4RollDiagnosticsOpen)
      .Section()
      .Id("BaselineRollHoldDiagnostics")
      [
        UI::VerticalLayout()
            .Spacing(6.0F)
            [
              +UI::TextWrapped(
                    "PX4 v1.17 fixed-wing Roll Hold state for the Baseline "
                    "simulation.")
              + UI::KeyValueGrid("BaselineRollHoldDiagnosticValues")
                    .ColumnsPerRow(2)
                    .AddDouble("PX4 Aileron",
                         props.px4RollAileronCommand,
                         "%.3f")
                    .AddDouble("PX4 Roll Error",
                         props.px4RollErrorDeg,
                         "%.2f deg")
                    .AddDouble("PX4 Rate SP",
                         props.px4RollRateSetpointDegPerSec,
                         "%.2f deg/s")
                    .AddDouble("Airspeed Scale",
                         props.px4AirspeedScaling,
                         "%.3f")
            ]
      ];
  // clang-format on
}
} // namespace

void BaselineAutopilotPanel::Draw(const BaselineAutopilotPanelProps &props) {
  const UI::UIElement layout = UI::VerticalLayout().Spacing(8.0F)
                               + UI::Heading("Autopilot Controls")
                               + MakeBaselineRollHoldSection(props);
  layout.Render();
}
} // namespace gui
