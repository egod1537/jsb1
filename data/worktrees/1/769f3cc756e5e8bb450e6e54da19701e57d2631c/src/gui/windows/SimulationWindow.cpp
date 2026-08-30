#include "gui/windows/SimulationWindow.hpp"

#include "flightui/FlightUI.hpp"

#include <string>
#include <vector>

namespace gui {
namespace UI = FlightUI;

namespace {
constexpr float InputWidth = 180.0F;
constexpr float LayoutSpacing = 8.0F;
} // namespace

SimulationWindow::SimulationWindow(SimulationController &controller)
    : Window("Simulation", EditorIconAliases::Simulation),
      controller_(controller) {}

void SimulationWindow::OnRender(const sim::SimulationSnapshot &snapshot) {
  controller_.Synchronize(snapshot);

  UI::TabGroup(
      "SimulationTabs")[+UI::Tab(
                            "Initial Condition")[UI::Custom([this, &snapshot] {
    DrawInitialConditionTab(snapshot);
  })] + UI::Tab("Diagnostics")[UI::Custom([this, &snapshot] {
    DrawDiagnosticsTab(snapshot);
  })] + UI::Tab("Environment")[UI::Custom([this] { DrawEnvironmentTab(); })]
                        + UI::Tab("Aircraft")[UI::Custom(
                            [this, &snapshot] { DrawAircraftTab(snapshot); })]]
      .Render();
}

void SimulationWindow::DrawInitialConditionTab(
    const sim::SimulationSnapshot &snapshot) {
  UI::VerticalLayout()
      .Spacing(LayoutSpacing)[+DrawInitialConditionFields()
                              + DrawInitialConditionActions(snapshot)
                              + DrawLastError(snapshot)]
      .Render();
}

void SimulationWindow::DrawDiagnosticsTab(
    const sim::SimulationSnapshot &snapshot) {
  const SimulationTransportProps transport = controller_.GetTransportProps();
  const double tickSizeSec = snapshot.config.simulationHz > 0.0
                                 ? 1.0 / snapshot.config.simulationHz
                                 : 0.0;

  UI::VerticalLayout()
      .Spacing(
          LayoutSpacing)[+UI::Heading("Diagnostics")
                         + UI::ValueLabel("Simulation Time",
                             snapshot.primary.aircraft.simulationTimeSec,
                             "%.2f s")
                         + UI::ValueLabel("Tick Size", tickSizeSec, "%.6f s")
                         + UI::ValueLabel("Pending Ticks",
                             static_cast<int>(transport.pendingTickCount),
                             "%d")
                         + DrawLastError(snapshot)]
      .Render();
}

void SimulationWindow::DrawEnvironmentTab() {
  UI::TextDisabled("Wind and atmosphere configuration will be added here.")
      .Render();
}

void SimulationWindow::DrawAircraftTab(
    const sim::SimulationSnapshot &snapshot) {
  const auto &engineStates = snapshot.primary.engines;

  UI::VerticalLayoutBuilder layout = UI::VerticalLayout().Spacing(LayoutSpacing)
                                     + UI::Heading("Aircraft")
                                     + UI::ValueLabel("Engine Count",
                                         static_cast<int>(engineStates.size()),
                                         "%d");

  if (engineStates.empty()) {
    layout =
        layout + UI::TextDisabled("No engines are defined for this aircraft.");
    static_cast<UI::UIElement>(layout).Render();
    return;
  }

  for (const sim::EngineState &engineState : engineStates) {
    layout =
        layout
        + UI::VerticalLayout().Spacing(
            2.0F)[+UI::Text("Engine " + std::to_string(engineState.index))
                  + UI::Text(std::string("Status: ")
                             + (engineState.running ? "Running" : "Stopped"))
                  + UI::ValueLabel("RPM", engineState.rpm, "%.2f")
                  + UI::ValueLabel("Throttle",
                      engineState.throttleCommand,
                      "%.3f")];
  }

  static_cast<UI::UIElement>(layout).Render();
}

UI::UIElement SimulationWindow::DrawInitialConditionFields() {
  const sim::InitialCondition &initialCondition =
      controller_.GetInitialConditionModel().pending;
  return UI::VerticalLayout().Spacing(LayoutSpacing)
      [+UI::Heading("Position")
          + UI::InputDouble("Latitude (deg)", initialCondition.latitudeDeg)
              .Width(InputWidth)
              .OnChanged([this](double value) {
                controller_.Handle({InitialConditionField::LatitudeDeg, value});
              })
          + UI::InputDouble("Longitude (deg)", initialCondition.longitudeDeg)
              .Width(InputWidth)
              .OnChanged([this](double value) {
                controller_.Handle(
                    {InitialConditionField::LongitudeDeg, value});
              })
          + UI::InputDouble("Altitude (ft)", initialCondition.altitudeFt)
              .Width(InputWidth)
              .OnChanged([this](double value) {
                controller_.Handle({InitialConditionField::AltitudeFt, value});
              })
          + UI::Heading("Attitude")
          + UI::InputDouble("Roll (deg)", initialCondition.rollDeg)
              .Width(InputWidth)
              .OnChanged([this](double value) {
                controller_.Handle({InitialConditionField::RollDeg, value});
              })
          + UI::InputDouble("Pitch (deg)", initialCondition.pitchDeg)
              .Width(InputWidth)
              .OnChanged([this](double value) {
                controller_.Handle({InitialConditionField::PitchDeg, value});
              })
          + UI::InputDouble("Heading (deg)", initialCondition.headingDeg)
              .Width(InputWidth)
              .OnChanged([this](double value) {
                controller_.Handle({InitialConditionField::HeadingDeg, value});
              })
          + UI::Heading("Velocity")
          + UI::InputDouble("Airspeed (kt)", initialCondition.airspeedKts)
              .Width(InputWidth)
              .OnChanged([this](double value) {
                controller_.Handle({InitialConditionField::AirspeedKts, value});
              })];
}

UI::UIElement SimulationWindow::DrawInitialConditionActions(
    const sim::SimulationSnapshot &snapshot) {
  const sim::InitialCondition currentCondition =
      snapshot.primary.currentCondition;
  const sim::InitialCondition defaultCondition =
      snapshot.defaultInitialCondition;

  return UI::HorizontalLayout().Spacing(
      LayoutSpacing)[+UI::Button("Reset With IC").OnAction([this] {
    controller_.Handle(ResetWithInitialConditionRequested{});
  }) + UI::Button("Use Current State").OnAction([this, currentCondition] {
    controller_.Handle(UseCurrentInitialConditionRequested{currentCondition});
  }) + UI::Button("Reset Default").OnAction([this, defaultCondition] {
    controller_.Handle(
        RestoreDefaultInitialConditionRequested{defaultCondition});
  })];
}

UI::UIElement SimulationWindow::DrawLastError(
    const sim::SimulationSnapshot &snapshot) const {
  const std::string &lastError = snapshot.status.lastError;
  if (lastError.empty()) {
    return {};
  }

  return UI::TextWrapped("Error: " + lastError);
}
} // namespace gui
