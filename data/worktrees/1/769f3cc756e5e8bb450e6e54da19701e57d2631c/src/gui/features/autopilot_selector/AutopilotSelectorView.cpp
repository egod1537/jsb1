#include "gui/features/autopilot_selector/AutopilotSelectorView.hpp"

#include "flightui/core/UIScale.hpp"

#include <imgui.h>

#include <algorithm>

namespace gui {
namespace {
constexpr float SelectorHeight = 32.0F;
constexpr float SelectorSpacing = 4.0F;
constexpr float SelectorRounding = 4.0F;

bool RenderSegment(const char *label, bool selected, bool enabled,
    float width) {
  ImGui::BeginDisabled(!enabled);
  if (selected) {
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    ImGui::PushStyleColor(ImGuiCol_Border,
        ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
  }

  const bool clicked =
      ImGui::Button(label, ImVec2(width, FlightUI::Ui(SelectorHeight)));

  if (selected) {
    ImGui::PopStyleColor(4);
  }
  ImGui::EndDisabled();
  return enabled && clicked;
}
} // namespace

void AutopilotSelectorView::Render(const AutopilotViewState &model,
    const AutopilotSelectorProps &props,
    architecture::EventSink<AutopilotSourceSelected> events) const {
  const AutopilotSelection selection = model.GetSelection();
  ImGui::TextDisabled("EDIT AUTOPILOT");
  const float spacing = FlightUI::Ui(SelectorSpacing);
  const float segmentWidth =
      std::max((ImGui::GetContentRegionAvail().x - spacing) * 0.5F, 1.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,
      FlightUI::Ui(SelectorRounding));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
  if (RenderSegment("PRIMARY",
          selection == AutopilotSelection::Primary,
          true,
          segmentWidth)) {
    events.Emit({AutopilotSelection::Primary});
  }
  ImGui::SameLine(0.0F, spacing);
  if (RenderSegment("BASELINE",
          selection == AutopilotSelection::Baseline,
          props.baselineAvailable,
          segmentWidth)) {
    events.Emit({AutopilotSelection::Baseline});
  }
  ImGui::PopStyleVar(2);
  if (!props.baselineAvailable
      && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("Baseline autopilot is not available");
  }
  ImGui::Spacing();
}
} // namespace gui
