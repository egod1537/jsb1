#include "gui/panels/AutopilotPanel.hpp"

#include "flightui/FlightUI.hpp"

namespace gui {
namespace UI = FlightUI;

namespace {
constexpr float AutopilotTargetInputWidth = 140.0F;
constexpr float HoldCaptureButtonWidth = 96.0F;
constexpr float AutopilotParameterLabelWidth = 148.0F;

UI::UIElement MakeAutopilotTargetRow(const char *targetLabel,
    const char *inputId, bool enabled, double targetValue,
    architecture::EventSink<PrimaryRollHoldValueChanged> events,
    double step = 1.0, double fastStep = 10.0) {
  return UI::HorizontalLayout().Spacing(
      8.0F)[+UI::TextDisabled(targetLabel)
            + UI::InputDouble(inputId, targetValue)
                .Width(AutopilotTargetInputWidth)
                .Step(step)
                .FastStep(fastStep)
                .Format("%.2f")
                .OnChanged([events](double value) {
                  events.Emit({PrimaryRollHoldField::TargetDeg, value});
                })
            + UI::Text(enabled ? "Hold" : "Off")];
}

UI::UIElement MakeRollHoldStatusRow(const AutopilotPanelProps &props) {
  // clang-format off
  return UI::HorizontalLayout()
      .Spacing(8.0F)
      [
        +UI::ValueLabel("Current Roll", props.currentRollDeg, "%.2f deg")
        + UI::ValueLabel(
              "Roll Rate", props.currentRollRateDegPerSec, "%.2f deg/s")
        + UI::ValueLabel("Aileron", props.currentAileron, "%.3f")
        + UI::StatusBadge(props.state.rollHold ? "Not Implemented" : "Inactive",
              props.state.rollHold ? UI::StatusTone::Warning
                                   : UI::StatusTone::Neutral)
        + UI::Button("Capture")
              .Enabled(props.events.IsConnected())
              .OnAction([events = props.events,
                            value = props.currentRollDeg] {
                events.Emit({PrimaryRollHoldField::TargetDeg, value});
              })
              .Width(HoldCaptureButtonWidth)
      ];
  // clang-format on
}

UI::UIElement MakeRollHoldParametersFoldOut(AutopilotPanelState &state,
    architecture::EventSink<PrimaryRollHoldValueChanged> events) {
  UI::PropertyGridBuilder parameters =
      UI::PropertyGrid("PrimaryRollHoldParameters")
          .LabelWidth(AutopilotParameterLabelWidth)
          .AlternatingRows();
  parameters
      .Add(UI::PropertyRow(
          "Roll Angle P Gain")[UI::ScalarEditor("RollAngleProportionalGain",
          state.rollAngleProportionalGain)
              .ShowSlider(false)
              .Step(0.01)
              .FastStep(0.1)
              .Format("%.2f")
              .OnChanged([events](double value) {
                events.Emit(
                    {PrimaryRollHoldField::AngleProportionalGain, value});
              })])
      .Add(UI::PropertyRow(
          "Roll Rate P Gain")[UI::ScalarEditor("RollRateProportionalGain",
          state.rollRateProportionalGain)
              .ShowSlider(false)
              .Step(0.01)
              .FastStep(0.1)
              .Format("%.2f")
              .OnChanged([events](double value) {
                events.Emit(
                    {PrimaryRollHoldField::RateProportionalGain, value});
              })]);

  return UI::FoldOut("P-P Parameters")
      .Open(state.rollHoldParametersOpen)
      .Section()
      .Id("RollHoldParameters")[parameters];
}

UI::UIElement MakeRollHoldSection(const AutopilotPanelProps &props) {
  AutopilotPanelState &state = props.state;
  const UI::UIElement content =
      UI::VerticalLayout().Spacing(6.0F)
      + MakeAutopilotTargetRow("Target Roll (deg)",
          "##RollHoldTarget",
          state.rollHold,
          state.rollTargetDeg,
          props.events)
      + MakeRollHoldStatusRow(props)
      + MakeRollHoldParametersFoldOut(state, props.events);

  return UI::ToggleFoldOut("Roll Hold", state.rollHold)
      .Id("RollHoldSection")
      .DefaultOpen()
      .OnChanged([events = props.events](bool enabled) {
        events.Emit({PrimaryRollHoldField::Enabled, enabled ? 1.0 : 0.0});
      })[content];
}

} // namespace

void AutopilotPanel::Draw(const AutopilotPanelProps &props) {
  const UI::UIElement layout = UI::VerticalLayout().Spacing(8.0F)
                               + UI::Heading("Autopilot Controls")
                               + MakeRollHoldSection(props);
  layout.Render();
}
} // namespace gui
