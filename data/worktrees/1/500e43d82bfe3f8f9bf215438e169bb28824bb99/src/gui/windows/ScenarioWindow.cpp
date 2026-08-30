#include "gui/windows/ScenarioWindow.hpp"

#include "flightui/FlightUI.hpp"
#include "sim/execution/ExecutionVariant.hpp"
#include "sim/runtime/SimulationContracts.hpp"

#include <imgui.h>

#include <cstdio>
#include <string>

namespace gui {
namespace UI = FlightUI;

namespace {
constexpr float InitialWindowWidth = 430.0F;
constexpr float InitialWindowHeight = 620.0F;
constexpr float FieldLabelWidthRatio = 0.44F;
constexpr float MinimumFieldLabelWidth = 110.0F;
constexpr float MaximumFieldLabelWidth = 180.0F;
constexpr float MinimumTwoColumnWidth = 320.0F;

UI::PropertyGridBuilder MakeScenarioPropertyGrid(const char *id) {
  return UI::PropertyGrid(id)
      .LabelWidthRatio(FieldLabelWidthRatio)
      .MinimumLabelWidth(MinimumFieldLabelWidth)
      .MaximumLabelWidth(MaximumFieldLabelWidth)
      .SingleColumnThreshold(MinimumTwoColumnWidth)
      .AlternatingRows();
}

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

const char *CommandTypeLabel(sim::ScenarioCommandType type) {
  switch (type) {
  case sim::ScenarioCommandType::RollHold:
    return "Roll hold";
  }
  return "Unknown";
}

void DrawIdentity(const sim::ResolvedExecutionSpec &execution) {
  const sim::SimulationScenario &scenario = execution.scenario;
  UI::PropertyGridBuilder fields = MakeScenarioPropertyGrid("CurrentIdentity");
  fields.Add("Scenario", UI::Text(scenario.name))
      .Add("Scenario Type", UI::Text(scenario.scenarioType))
      .Add("Schema Version", UI::Text(std::to_string(scenario.schemaVersion)))
      .Add("Aircraft", UI::Text(scenario.aircraft))
      .Add("Autopilot", UI::Text(std::string(sim::ToString(execution.variant))))
      .Add("Duration",
          UI::ValueLabel("##Duration", scenario.durationSec, "%.3f s"))
      .Add("Time Step", UI::ValueLabel("##TimeStep", scenario.dtSec, "%.6f s"));
  static_cast<UI::UIElement>(fields).Render();
}

void DrawInitialCondition(const sim::InitialCondition &condition) {
  ImGui::SeparatorText("Initial Condition");
  UI::PropertyGridBuilder fields = MakeScenarioPropertyGrid("CurrentInitial");
  fields
      .Add("Latitude",
          UI::ValueLabel("##Latitude", condition.latitudeDeg, "%.3f deg"))
      .Add("Longitude",
          UI::ValueLabel("##Longitude", condition.longitudeDeg, "%.3f deg"))
      .Add("Altitude",
          UI::ValueLabel("##Altitude", condition.altitudeFt, "%.3f ft"))
      .Add("Airspeed",
          UI::ValueLabel("##Airspeed", condition.airspeedKts, "%.3f kt"))
      .Add("Roll", UI::ValueLabel("##Roll", condition.rollDeg, "%.3f deg"))
      .Add("Pitch", UI::ValueLabel("##Pitch", condition.pitchDeg, "%.3f deg"))
      .Add("Heading",
          UI::ValueLabel("##Heading", condition.headingDeg, "%.3f deg"));
  static_cast<UI::UIElement>(fields).Render();
}

void DrawConditions(const sim::SimulationScenario &scenario) {
  ImGui::SeparatorText("Conditions");
  UI::PropertyGridBuilder fields =
      MakeScenarioPropertyGrid("CurrentConditions");
  fields.Add("Wind", UI::Text(scenario.windEnabled ? "Enabled" : "Disabled"))
      .Add("Trim", UI::Text(scenario.runTrim ? "Enabled" : "Disabled"))
      .Add("Trim Mode", UI::Text(TrimModeLabel(scenario.trimMode)));
  static_cast<UI::UIElement>(fields).Render();
}

void DrawEvents(const sim::SimulationScenario &scenario) {
  ImGui::SeparatorText("Events");
  if (scenario.events.empty()) {
    UI::TextDisabled("No events.").Render();
    return;
  }

  for (const sim::ScenarioEventDefinition &event : scenario.events) {
    char summary[160]{};
    std::snprintf(summary,
        sizeof(summary),
        "%.3f s   %s = %.3f deg",
        event.timeSec,
        CommandTypeLabel(event.command.type),
        event.command.rollDeg);
    UI::Text(summary).Render();
  }
}

void DrawAcceptance(const sim::SimulationScenario &scenario) {
  ImGui::SeparatorText("Acceptance Criteria");
  UI::PropertyGridBuilder fields =
      MakeScenarioPropertyGrid("CurrentAcceptance");
  fields
      .Add("Settling Band",
          UI::ValueLabel("##SettlingBand",
              scenario.settlingBandDeg,
              "%.3f deg"))
      .Add("Settling Limit",
          UI::ValueLabel("##SettlingLimit",
              scenario.settlingTimeLimitSec,
              "%.3f s"))
      .Add("Overshoot Limit",
          UI::ValueLabel("##Overshoot", scenario.overshootLimitDeg, "%.3f deg"))
      .Add("Oscillation Cycles",
          UI::ValueLabel("##Oscillation",
              scenario.maxOscillationCycles,
              "%.3f"));
  static_cast<UI::UIElement>(fields).Render();
}
} // namespace

ScenarioWindow::ScenarioWindow()
    : Window("Current Scenario", EditorIconAliases::Scenario, "Scenario") {}

void ScenarioWindow::PrepareWindow() {
  ImGui::SetNextWindowSize(
      ImVec2(UI::Ui(InitialWindowWidth), UI::Ui(InitialWindowHeight)),
      ImGuiCond_FirstUseEver);
}

void ScenarioWindow::OnRender(const sim::SimulationSnapshot &snapshot) {
  UI::TextDisabled("Resolved values from the Scenario applied to the runtime.")
      .Render();
  ImGui::Spacing();
  if (!snapshot.appliedExecution.has_value()) {
    UI::TextDisabled("No Scenario is currently applied.").Render();
    return;
  }

  const sim::ResolvedExecutionSpec &execution = *snapshot.appliedExecution;
  DrawIdentity(execution);
  DrawInitialCondition(execution.scenario.initialCondition);
  DrawConditions(execution.scenario);
  DrawEvents(execution.scenario);
  DrawAcceptance(execution.scenario);

  if (!execution.source.file.empty()
      || !execution.source.digestSha256.empty()) {
    ImGui::SeparatorText("Source");
    UI::PropertyGridBuilder fields = MakeScenarioPropertyGrid("CurrentSource");
    fields.Add("File",
        UI::Text(execution.source.file.empty() ? "Embedded"
                                               : execution.source.file));
    if (!execution.source.digestSha256.empty()) {
      fields.Add("SHA-256", UI::Text(execution.source.digestSha256));
    }
    static_cast<UI::UIElement>(fields).Render();
  }
}
} // namespace gui
