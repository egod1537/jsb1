#include "gui/windows/samples/DeclarativeDemoWindow.hpp"

#include "flightui/FlightUI.hpp"

namespace gui {
namespace UI = FlightUI;

namespace {
constexpr std::size_t MaxHistorySamples = 240;
constexpr double SampleIntervalSec = 1.0 / 24.0;
} // namespace

DeclarativeDemoWindow::DeclarativeDemoWindow() = default;

DeclarativeDemoWindow::~DeclarativeDemoWindow() = default;

void DeclarativeDemoWindow::OnTick(const GUIFrameContext &) {
  AppendTelemetrySample();

  // clang-format off
  UI::Window("Declarative Flight Monitor")
      .InitialSize({1000.0F, 700.0F})
      [
        UI::HorizontalLayout()
            .Spacing(8.0F)
            [
              +UI::Panel("Controls")
                   .Width(300.0F)
                   .Border(true)
                   [
                     UI::VerticalLayout()
                     [
                       +UI::Heading("Flight Controls")
                       + UI::Toggle("Autopilot", autopilotEnabled_)
                             .OnChanged([this](bool value) {
                               autopilotEnabled_ = value;
                             })
                       + UI::SliderDouble("Throttle", throttle_, 0.0, 1.0)
                             .OnChanged([this](double value) {
                               throttle_ = value;
                             })
                             .Format("%.3f")
                       + UI::SliderDouble("Elevator", elevator_, -1.0, 1.0)
                             .OnChanged([this](double value) {
                               elevator_ = value;
                             })
                             .Format("%.3f")
                       + UI::Button("Reset")
                             .OnAction([this] {
                               throttle_ = 0.0;
                               elevator_ = 0.0;
                             })
                     ]
                   ]
              + UI::Panel("Telemetry")
                    .FlexibleWidth(true)
                    .Border(true)
                    [
                      UI::VerticalLayout()
                      [
                        +UI::HorizontalLayout()
                             .Spacing(16.0F)
                             [
                               +UI::ValueLabel("Throttle", throttle_, "%.3f")
                               + UI::ValueLabel("Elevator", elevator_, "%.3f")
                             ]
                        + UI::Plot("Commands")
                              .Height(300.0F)
                              .XAxisLabel("Time")
                              .YAxisLabel("Command")
                              .AddLine("Throttle", timeHistory_,
                                       throttleHistory_)
                              .AddLine("Elevator", timeHistory_,
                                       elevatorHistory_)
                      ]
                    ]
            ]
      ];
  // clang-format on
}

void DeclarativeDemoWindow::AppendTelemetrySample() {
  const double time = UI::GetTime();
  if (lastSampleTime_ >= 0.0 && time - lastSampleTime_ < SampleIntervalSec) {
    return;
  }

  lastSampleTime_ = time;
  timeHistory_.push_back(time);
  throttleHistory_.push_back(throttle_);
  elevatorHistory_.push_back(elevator_);

  if (timeHistory_.size() <= MaxHistorySamples) {
    return;
  }

  timeHistory_.erase(timeHistory_.begin());
  throttleHistory_.erase(throttleHistory_.begin());
  elevatorHistory_.erase(elevatorHistory_.begin());
}
} // namespace gui
