#include "gui/panels/TrimPanel.hpp"

#include "flightui/FlightUI.hpp"

namespace gui {
namespace UI = FlightUI;

namespace {
constexpr float TrimInputPanelHeight = 178.0F;
constexpr float TrimLayoutSpacing = 8.0F;
constexpr float TrimButtonWidth = 160.0F;

const char *TrimModeLabel(gnc::TrimMode mode) {
  switch (mode) {
  case gnc::TrimMode::Longitudinal:
    return "Longitudinal";
  case gnc::TrimMode::Full:
    return "Full";
  case gnc::TrimMode::Ground:
    return "Ground";
  }

  return "Unknown";
}

int TrimModeIndex(gnc::TrimMode mode) {
  switch (mode) {
  case gnc::TrimMode::Longitudinal:
    return 0;
  case gnc::TrimMode::Full:
    return 1;
  case gnc::TrimMode::Ground:
    return 2;
  }

  return 0;
}

gnc::TrimMode TrimModeFromIndex(int index) {
  switch (index) {
  case 1:
    return gnc::TrimMode::Full;
  case 2:
    return gnc::TrimMode::Ground;
  case 0:
  default:
    return gnc::TrimMode::Longitudinal;
  }
}

UI::UIElement MakeTrimInputPanel(const gnc::TrimRequest &request,
    architecture::EventSink<TrimRequestValueChanged> events) {
  return UI::Panel("TrimInputPanel")
      .FlexibleWidth(true)
      .Height(TrimInputPanelHeight)
      .Border(true)[UI::VerticalLayout().Spacing(
          6.0F)[+UI::Heading("Trim Input")
                + UI::Combo("Mode",
                    TrimModeIndex(request.mode),
                    {"Longitudinal", "Full", "Ground"})
                    .OnChanged([events](int index) {
                      events.Emit(
                          {TrimRequestField::Mode, static_cast<double>(index)});
                    })
                + UI::InputDouble("Airspeed (kt)", request.airspeedKts)
                    .Step(1.0)
                    .FastStep(10.0)
                    .Format("%.2f")
                    .OnChanged([events](double value) {
                      events.Emit({TrimRequestField::AirspeedKts, value});
                    })
                + UI::InputDouble("Altitude (ft)", request.altitudeFt)
                    .Step(100.0)
                    .FastStep(1000.0)
                    .Format("%.2f")
                    .OnChanged([events](double value) {
                      events.Emit({TrimRequestField::AltitudeFt, value});
                    })
                + UI::InputDouble("Flight Path Angle (deg)",
                    request.flightPathAngleDeg)
                    .Step(0.1)
                    .FastStep(1.0)
                    .Format("%.2f")
                    .OnChanged([events](double value) {
                      events.Emit(
                          {TrimRequestField::FlightPathAngleDeg, value});
                    })]];
}

UI::UIElement MakeTrimRequestSummary(const gnc::TrimRequest &request) {
  return UI::KeyValueGrid("TrimRequestSummaryTable")
      .ColumnsPerRow(4)
      .Add("Mode", TrimModeLabel(request.mode))
      .AddDouble("Airspeed", request.airspeedKts, "%.2f kt")
      .AddDouble("Altitude", request.altitudeFt, "%.2f ft")
      .AddDouble("Flight Path Angle", request.flightPathAngleDeg, "%.2f deg");
}

UI::UIElement MakeTrimResultContent(const gnc::TrimResult &result,
    bool hasResult) {
  const UI::StatusTone statusTone = !hasResult       ? UI::StatusTone::Neutral
                                    : result.success ? UI::StatusTone::Success
                                                     : UI::StatusTone::Error;
  UI::VerticalLayoutBuilder layout =
      UI::VerticalLayout().Spacing(6.0F)
      + UI::HorizontalLayout().Spacing(
          6.0F)[+UI::TextDisabled("Status")
                + UI::StatusBadge(hasResult
                                      ? (result.success ? "Success" : "Failed")
                                      : "Idle",
                    statusTone)];

  if (!result.message.empty()) {
    layout = layout
             + UI::HorizontalLayout().Spacing(
                 6.0F)[+UI::TextDisabled("Message")
                       + UI::TextWrapped(result.message)];
  }

  layout = layout
           + UI::KeyValueGrid("TrimResultMetrics")
                 .ColumnsPerRow(2)
                 .AddDouble("Alpha", result.alphaDeg, "%.2f deg")
                 .AddDouble("Beta", result.betaDeg, "%.2f deg")
                 .AddDouble("Roll", result.rollDeg, "%.2f deg")
                 .AddDouble("Pitch", result.pitchDeg, "%.2f deg")
                 .AddDouble("Throttle", result.throttle, "%.3f")
                 .AddDouble("Elevator", result.elevator, "%.3f")
                 .AddDouble("Pitch Trim", result.pitchTrim, "%.3f")
                 .AddDouble("Aileron", result.aileron, "%.3f")
                 .AddDouble("Rudder", result.rudder, "%.3f");

  return layout;
}

UI::UIElement MakeTrimResidualContent(const gnc::TrimResult &result) {
  return UI::KeyValueGrid("TrimResidualMetrics")
      .ColumnsPerRow(2)
      .AddDouble("uDot", result.uDot, "%.4f m/s^2")
      .AddDouble("vDot", result.vDot, "%.4f m/s^2")
      .AddDouble("wDot", result.wDot, "%.4f m/s^2")
      .AddDouble("pDot", result.pDot, "%.4f deg/s^2")
      .AddDouble("qDot", result.qDot, "%.4f deg/s^2")
      .AddDouble("rDot", result.rDot, "%.4f deg/s^2");
}
} // namespace

void TrimPanel::Draw(TrimPanelProps props) {
  UI::VerticalLayout()
      .Spacing(TrimLayoutSpacing)
          [+MakeTrimInputPanel(props.request, props.valueEvents)
              + UI::HorizontalLayout().Spacing(
                  8.0F)[+UI::Button("RunIC Trim")
                            .Width(TrimButtonWidth)
                            .Enabled(props.canRequestTrim)
                            .OnAction([events = props.executionEvents] {
                              events.Emit({false});
                            })
                        + UI::Button("Current State Trim")
                            .Width(TrimButtonWidth)
                            .Enabled(props.canRequestTrim)
                            .OnAction([events = props.executionEvents] {
                              events.Emit({true});
                            })]
              + MakeTrimRequestSummary(props.request) + UI::Space(6.0F)
              + UI::FoldOut("Result").Open(
                  props.resultOpen)[MakeTrimResultContent(props.result,
                  props.hasResult)]
              + UI::FoldOut("Residual")
                  .Open(props
                          .residualOpen)[MakeTrimResidualContent(props.result)]]
      .Render();
}
} // namespace gui
