#include "GNCWindow.hpp"

#include "gui/panels/AutopilotPanel.hpp"
#include "gui/panels/BaselineAutopilotPanel.hpp"
#include "gui/panels/ManualControlPanel.hpp"
#include "gui/panels/TrimPanel.hpp"
#include "sim/control/FlightControlMode.hpp"
#include "common/math/Math.hpp"
#include "flightui/FlightUI.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace gui {
namespace UI = FlightUI;

namespace {
constexpr float AutopilotSelectorRounding = 4.0F;
constexpr float AutopilotIdentityHeight = 34.0F;
constexpr float AutopilotIdentityAccentWidth = 3.0F;
constexpr float AutopilotIdentityPadding = 12.0F;

bool IsBaselineAutopilotAvailable(const sim::SimulationSnapshot &snapshot) {
  return snapshot.baseline.has_value() && snapshot.baselineAutopilot.has_value()
         && snapshot.baselineAutopilot->available;
}

const sim::AutopilotSnapshot *GetAutopilotSnapshot(
    const sim::SimulationSnapshot &snapshot, AutopilotSelection selection) {
  if (selection == AutopilotSelection::Baseline) {
    return snapshot.baselineAutopilot ? &*snapshot.baselineAutopilot : nullptr;
  }
  return &snapshot.primaryAutopilot;
}

const sim::SimulationInstanceSnapshot *GetInstanceSnapshot(
    const sim::SimulationSnapshot &snapshot, AutopilotSelection selection) {
  if (selection == AutopilotSelection::Baseline) {
    return snapshot.baseline ? &*snapshot.baseline : nullptr;
  }
  return &snapshot.primary;
}

void DrawAutopilotIdentityHeader(AutopilotSelection selection,
    const char *strategyName) {
  const char *role =
      selection == AutopilotSelection::Primary ? "PRIMARY" : "BASELINE";
  const float height = UI::Ui(AutopilotIdentityHeight);
  const float width = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
  const ImVec2 minimum = ImGui::GetCursorScreenPos();
  const ImVec2 maximum{minimum.x + width, minimum.y + height};
  ImGui::Dummy(ImVec2(width, height));

  ImDrawList &drawList = *ImGui::GetWindowDrawList();
  drawList.AddRectFilled(minimum,
      maximum,
      ImGui::GetColorU32(ImGuiCol_FrameBg));
  drawList.AddRect(minimum,
      maximum,
      ImGui::GetColorU32(ImGuiCol_Border),
      UI::Ui(AutopilotSelectorRounding));
  drawList.AddRectFilled(minimum,
      ImVec2(minimum.x + UI::Ui(AutopilotIdentityAccentWidth), maximum.y),
      ImGui::GetColorU32(ImGuiCol_CheckMark),
      UI::Ui(AutopilotSelectorRounding));

  const std::string roleLabel = std::string(role) + "  ·  ";
  const ImVec2 textSize = ImGui::CalcTextSize(roleLabel.c_str());
  const ImVec2 textPosition{minimum.x + UI::Ui(AutopilotIdentityPadding),
      minimum.y + (height - ImGui::GetTextLineHeight()) * 0.5F};
  drawList.PushClipRect(minimum, maximum, true);
  drawList.AddText(textPosition,
      ImGui::GetColorU32(ImGuiCol_CheckMark),
      roleLabel.c_str());
  drawList.AddText(ImVec2(textPosition.x + textSize.x, textPosition.y),
      ImGui::GetColorU32(ImGuiCol_Text),
      strategyName);
  drawList.PopClipRect();
  ImGui::Spacing();
}
} // namespace

GNCWindow::GNCWindow() : Window("GNC", EditorIconAliases::GNC) {}

GNCWindow::GNCWindow(GNCController &controller) : GNCWindow() {
  controller_ = &controller;
}

void GNCWindow::OnRender(const sim::SimulationSnapshot &snapshot) {
  if (!snapshot.primary.available || !snapshot.primaryAutopilot.available) {
    ImGui::TextDisabled("Primary autopilot is unavailable.");
    return;
  }

  controller_->Synchronize(snapshot);
  GNCModel model = controller_->GetModel();

  const bool baselineAvailable = IsBaselineAutopilotAvailable(snapshot);
  const AutopilotSelectorProps autopilotSelectorProps{baselineAvailable};
  autopilotSelector_.Update(autopilotSelectorProps);
  const gnc::TrimResult emptyTrimResult{};
  const gnc::TrimResult *trimResult =
      snapshot.trim.result ? &*snapshot.trim.result : nullptr;
  const bool trimHasResult = trimResult != nullptr;

  // clang-format off
  UI::VerticalLayout()
      [
        +UI::TabGroup("GNC")
            [
              +UI::Tab("Trim")
                  [
                    UI::Custom([this,
                                   &model,
                                   emptyTrimResult,
                                   trimResult,
                                   trimHasResult] {
                      TrimPanel::Draw({
                          model.trimRequest,
                          trimHasResult ? *trimResult : emptyTrimResult,
                          trimHasResult,
                          model.trimResultOpen,
                          model.trimResidualOpen,
                          !model.trimInProgress,
                          architecture::EventSink<TrimRequestValueChanged>{
                              [this](const TrimRequestValueChanged &event) {
                                controller_->Handle(event);
                              }},
                          architecture::EventSink<TrimExecutionRequested>{
                              [this](const TrimExecutionRequested &event) {
                                controller_->Handle(event);
                              }},
                      });
                    })
                  ]
              + UI::Tab("Autopilot")
                    [
                      UI::Custom([this,
                                     &snapshot,
                                     &model,
                                     autopilotSelectorProps] {
                        autopilotSelector_.Render(autopilotSelectorProps);
                        const AutopilotSelection selection =
                            autopilotSelector_.GetModel().GetSelection();
                        const sim::AutopilotSnapshot *autopilot =
                            GetAutopilotSnapshot(snapshot, selection);
                        const sim::SimulationInstanceSnapshot *instance =
                            GetInstanceSnapshot(snapshot, selection);
                        if (autopilot == nullptr || instance == nullptr
                            || !autopilot->available || !instance->available) {
                          DrawAutopilotIdentityHeader(selection, "Unavailable");
                          ImGui::TextDisabled("Autopilot data is unavailable.");
                          return;
                        }

                        DrawAutopilotIdentityHeader(
                            selection, autopilot->strategyName.c_str());
                        if (selection == AutopilotSelection::Baseline) {
                          const sim::BaselineRollHoldDiagnostics &diagnostics =
                              autopilot->baselineDiagnostics;
                          const double currentRollDeg = instance->aircraft.rollDeg;
                          BaselineAutopilotPanel::Draw({
                              .state = model.baselineAutopilot,
                              .currentRollDeg = currentRollDeg,
                              .currentRollRateDegPerSec =
                                  instance->aircraft.pDegPerSec,
                              .currentAileron = instance->controlInput.aileron,
                              .rollHoldActive =
                                  autopilot->mode
                                      == control::FlightControlMode::Autopilot
                                  && autopilot->baselineRollHold.enabled,
                              .valueEvents = architecture::EventSink<BaselineRollHoldValueChanged>{
                                  [this](const BaselineRollHoldValueChanged &event) {
                                    controller_->Handle(event);
                                  }},
                              .resetEvents = architecture::EventSink<BaselineRollHoldTuningResetRequested>{
                                  [this](const BaselineRollHoldTuningResetRequested &event) {
                                    controller_->Handle(event);
                                  }},
                              .px4RollAileronCommand =
                                  diagnostics.aileronCommand,
                              .px4RollRateSetpointDegPerSec = math::RadToDeg(
                                  diagnostics.bodyRateSetpointRadPerSec),
                              .px4RollErrorDeg =
                                  math::RadToDeg(diagnostics.rollErrorRad),
                              .px4AirspeedScaling = diagnostics.airspeedScaling,
                          });
                          return;
                        }

                        const double currentRollDeg = instance->aircraft.rollDeg;
                        AutopilotPanel::Draw({
                            .state = model.primaryAutopilot,
                            .currentRollDeg = currentRollDeg,
                            .currentRollRateDegPerSec =
                                instance->aircraft.pDegPerSec,
                            .currentAileron = instance->controlInput.aileron,
                            .events = architecture::EventSink<PrimaryRollHoldValueChanged>{
                                [this](const PrimaryRollHoldValueChanged &event) {
                                  controller_->Handle(event);
                                }},
                        });
                      })
                    ]
              + UI::Tab("Flight Control")
                    [
                      UI::Custom([this, &snapshot, &model] {
                        ManualControlPanel::Draw({
                            .autopilotState = model.primaryAutopilot,
                            .input = snapshot.primaryAutopilot.manualControl,
                            .pitchTrim = snapshot.primary.pitchTrim,
                            .events = architecture::EventSink<ManualControlChanged>{
                                [this](const ManualControlChanged &event) {
                                  controller_->Handle(event);
                                }},
                        });
                      })
                    ]
            ]
      ]
      .Render();
  // clang-format on

  controller_->Handle(
      GNCViewStateChanged{model.primaryAutopilot.rollHoldParametersOpen,
          model.baselineAutopilot.px4RollTuningOpen,
          model.baselineAutopilot.px4RollDiagnosticsOpen,
          model.trimResultOpen,
          model.trimResidualOpen});
  controller_->PublishConfiguration(snapshot);
}
} // namespace gui
