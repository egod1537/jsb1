#include "gui/features/monitor/MonitorView.hpp"

#include "gui/features/monitor/plots/RollTrackingAcceptance.hpp"
#include "sim/linearization/DynamicModeContracts.hpp"
#include "sim/telemetry/AircraftTelemetry.hpp"
#include "sim/telemetry/AutopilotTelemetry.hpp"
#include "sim/telemetry/TelemetryContracts.hpp"
#include "flightui/FlightUI.hpp"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gui {
namespace UI = FlightUI;

namespace {
constexpr float ExplorerMinWidth = 180.0F;
constexpr float ExplorerMaxWidth = 600.0F;
constexpr float ExplorerMinimumPlotWidth = 320.0F;
constexpr float ExplorerCollapsedWidth = 30.0F;
constexpr float PaneSplitterThickness = 6.0F;
constexpr float PlotHeight = 245.0F;
constexpr float MinimumGridPlotHeight = 105.0F;
constexpr float WorkspaceSpacing = 8.0F;
constexpr float PlotCardTopMargin = 3.0F;
constexpr float PlotTitleFrameSpacing = 5.0F;
constexpr float PlotCardBottomMargin = 12.0F;
constexpr float PlotGridCellPadding = 4.0F;
constexpr float TelemetryTreeIndentSpacing = 12.0F;
constexpr float TelemetryTreeControlSpacing = 5.0F;
constexpr float TimelineMinHeight = 200.0F;
constexpr float TimelineMaxHeight = 300.0F;
constexpr float TimelineCollapsedHeight = 32.0F;
constexpr float TimelineOverviewBarHeight = 12.0F;
constexpr float TimelineDetailBarHeight = 18.0F;
constexpr float LinearizationTrackHeight = 14.0F;
constexpr float LinearizationMarkerRadius = 3.5F;
constexpr float LinearizationMarkerHitRadius = 7.0F;
constexpr float TimelineHandleWidth = 10.0F;
constexpr float TimelineHorizontalPadding = 12.0F;
constexpr float TimelineRowSpacing = 7.0F;
constexpr double MinimumTimelineWindowSec = 0.1;
constexpr double TimelineZoomFactor = 1.15;
constexpr int TargetTimelineTickCount = 6;
constexpr std::size_t MaximumPresetPlots = 16;
constexpr std::size_t MaximumDisplayedModeStates = 6;
constexpr std::size_t MinimumRenderedSamplesPerChannel = 512;
constexpr std::size_t MaximumRenderedSamplesPerChannel = 4096;
constexpr double MinimumDisplayedParticipation = 0.05;

const gnc::DynamicModeSnapshot *FindLatestDynamicModeAtOrBefore(
    std::span<const gnc::DynamicModeSnapshot> history, double timeSec) {
  const auto snapshot = std::upper_bound(history.begin(),
      history.end(),
      timeSec,
      [](double time, const gnc::DynamicModeSnapshot &candidate) {
        return time < candidate.simulationTimeSec;
      });
  if (snapshot == history.begin()) {
    return nullptr;
  }
  return &*std::prev(snapshot);
}

void DrawDashedPlotLine(const std::vector<telemetry::TelemetrySample> &samples,
    double valueOffset, ImU32 color, float dashLength, float gapLength,
    float thickness) {
  if (samples.size() < 2) {
    return;
  }

  ImDrawList *drawList = ImPlot::GetPlotDrawList();
  const float patternLength = dashLength + gapLength;
  float patternOffset = 0.0F;

  for (std::size_t sampleIndex = 1; sampleIndex < samples.size();
      ++sampleIndex) {
    const telemetry::TelemetrySample &previous = samples[sampleIndex - 1];
    const telemetry::TelemetrySample &current = samples[sampleIndex];
    if (!std::isfinite(previous.timeSec) || !std::isfinite(previous.value)
        || !std::isfinite(current.timeSec) || !std::isfinite(current.value)) {
      continue;
    }

    const ImVec2 start =
        ImPlot::PlotToPixels(previous.timeSec, previous.value + valueOffset);
    const ImVec2 end =
        ImPlot::PlotToPixels(current.timeSec, current.value + valueOffset);
    const ImVec2 delta(end.x - start.x, end.y - start.y);
    const float segmentLength =
        std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (segmentLength <= 0.001F) {
      continue;
    }

    float segmentOffset = 0.0F;
    while (segmentOffset < segmentLength) {
      const float positionInPattern = std::fmod(patternOffset, patternLength);
      const bool drawing = positionInPattern < dashLength;
      const float runLength = drawing ? dashLength - positionInPattern
                                      : patternLength - positionInPattern;
      const float nextOffset =
          std::min(segmentLength, segmentOffset + runLength);
      if (drawing && nextOffset > segmentOffset) {
        const float startRatio = segmentOffset / segmentLength;
        const float endRatio = nextOffset / segmentLength;
        drawList->AddLine(ImVec2(start.x + delta.x * startRatio,
                              start.y + delta.y * startRatio),
            ImVec2(start.x + delta.x * endRatio, start.y + delta.y * endRatio),
            color,
            thickness);
      }
      const float advanced = nextOffset - segmentOffset;
      segmentOffset = nextOffset;
      patternOffset += advanced;
    }
  }
}

template <typename T>
T ClampToOrderedRange(T value, T firstBound, T secondBound) {
  const T minimum = std::min(firstBound, secondBound);
  const T maximum = std::max(firstBound, secondBound);
  return std::min(std::max(value, minimum), maximum);
}

enum class DefaultTelemetryPlot {
  AerodynamicAngles,
  Attitude,
  BodyVelocities,
  BodyRates,
  Airspeed,
  AltitudeAgl,
  BodyAccelerations,
  AngularAccelerations,
  RollHoldRollTracking,
  RollHoldRollError,
  RollHoldRollRateTracking,
  RollHoldRollRateError,
  Px4RollHoldRateControllerTerms,
  RollHoldControlOutput,
  Px4RollHoldIntegrator,
  Px4RollHoldCalibratedAirspeed,
  Px4RollHoldAirspeedScaling,
  Px4RollHoldLateralRates,
  Px4RollHoldSideslip,
  Px4RollHoldLateralControls,
  Px4RollHoldSaturationStatus,
  Count,
};

struct TelemetryPlotBinding {
  std::string_view nodePath;
  std::string_view plotTitle;
  std::string_view yAxisLabel;
};

struct TelemetryDisplayInfo {
  std::string_view rawName;
  std::string_view displayName;
};

constexpr std::array TelemetryDisplayNames{
    TelemetryDisplayInfo{"alpha", "Alpha"},
    TelemetryDisplayInfo{"beta", "Beta"},
    TelemetryDisplayInfo{"roll", "Roll"},
    TelemetryDisplayInfo{"pitch", "Pitch"},
    TelemetryDisplayInfo{"heading", "Heading"},
    TelemetryDisplayInfo{"course", "Course"},
    TelemetryDisplayInfo{"p", "P"},
    TelemetryDisplayInfo{"q", "Q"},
    TelemetryDisplayInfo{"r", "R"},
    TelemetryDisplayInfo{"p_dot", "p\xCC\x87"},
    TelemetryDisplayInfo{"q_dot", "q\xCC\x87"},
    TelemetryDisplayInfo{"r_dot", "r\xCC\x87"},
    TelemetryDisplayInfo{"commanded_roll", "Commanded Roll"},
    TelemetryDisplayInfo{"roll_error", "Roll Error"},
    TelemetryDisplayInfo{"commanded_roll_rate", "Commanded Roll Rate"},
    TelemetryDisplayInfo{"roll_rate", "Roll Rate"},
    TelemetryDisplayInfo{"roll_rate_error", "Roll Rate Error"},
    TelemetryDisplayInfo{"rate_p_term", "P Term"},
    TelemetryDisplayInfo{"rate_i_term", "I Term"},
    TelemetryDisplayInfo{"rate_d_term", "D Term"},
    TelemetryDisplayInfo{"rate_ff_term", "FF Term"},
    TelemetryDisplayInfo{"unscaled_torque_command", "Unscaled Torque"},
    TelemetryDisplayInfo{"raw_torque_command", "Raw Torque"},
    TelemetryDisplayInfo{"roll_torque_command", "Saturated Torque"},
    TelemetryDisplayInfo{"airspeed_scaling", "Airspeed Scaling"},
    TelemetryDisplayInfo{"positive_saturation", "Positive Saturation"},
    TelemetryDisplayInfo{"negative_saturation", "Negative Saturation"},
    TelemetryDisplayInfo{"integrator_limited", "Integrator Limited"},
    TelemetryDisplayInfo{"trim_roll_command", "Roll Trim"},
    TelemetryDisplayInfo{"rate_integrator_positive_limit", "+I Limit"},
    TelemetryDisplayInfo{"rate_integrator_negative_limit", "-I Limit"},
    TelemetryDisplayInfo{"aileron_command", "Roll Hold Aileron Command"},
    TelemetryDisplayInfo{"calibrated_airspeed", "Calibrated Airspeed"},
    TelemetryDisplayInfo{"aileron", "Aileron"},
    TelemetryDisplayInfo{"rudder", "Rudder"},
};

std::string_view GetTelemetryDisplayName(std::string_view rawName) {
  const auto displayInfo = std::find_if(TelemetryDisplayNames.begin(),
      TelemetryDisplayNames.end(),
      [rawName](const TelemetryDisplayInfo &candidate) {
        return candidate.rawName == rawName;
      });
  return displayInfo == TelemetryDisplayNames.end() ? rawName
                                                    : displayInfo->displayName;
}

std::string MakeTelemetrySeriesLabel(std::string_view path,
    std::string_view sourceName) {
  const std::size_t separator = path.rfind('/');
  const std::string_view rawName =
      separator == std::string_view::npos ? path : path.substr(separator + 1);
  return std::string(sourceName) + " · "
         + std::string(GetTelemetryDisplayName(rawName)) + "##"
         + std::string(sourceName) + "/" + std::string(path);
}

constexpr std::array<TelemetryPlotBinding,
    static_cast<std::size_t>(DefaultTelemetryPlot::Count)>
    TelemetryPlotBindings{{
        {"aircraft/aero", "Aerodynamic Angles", "deg"},
        {"aircraft/attitude", "Attitude", "deg"},
        {"aircraft/body_velocity", "Body Velocities", "m/s"},
        {"aircraft/rates", "Body Rates", "deg/s"},
        {"aircraft/airdata", "Airspeed", "kt"},
        {"aircraft/position", "Altitude AGL", "ft"},
        {"aircraft/body_acceleration", "Body Accelerations", "m/s^2"},
        {"aircraft/angular_acceleration", "Angular Accelerations", "deg/s^2"},
        {"preset/roll_hold/roll_tracking", "Roll Attitude Tracking", "deg"},
        {"preset/px4_roll_hold/roll_error", "Roll Error", "deg"},
        {"preset/roll_hold/roll_rate_tracking", "Roll Rate Tracking", "deg/s"},
        {"preset/px4_roll_hold/roll_rate_error", "Roll Rate Error", "deg/s"},
        {"preset/px4_roll_hold/rate_terms",
            "PX4 Rate Controller Terms",
            "normalized"},
        {"preset/roll_hold/control_output",
            "Controller Output Pipeline",
            "normalized"},
        {"preset/px4_roll_hold/integrator", "Integrator", "normalized"},
        {"preset/px4_roll_hold/calibrated_airspeed",
            "Calibrated Airspeed",
            "kt"},
        {"preset/px4_roll_hold/airspeed_scaling",
            "Airspeed Scaling",
            "dimensionless"},
        {"preset/px4_roll_hold/lateral_rates",
            "Lateral Coupling Rates",
            "deg/s"},
        {"preset/px4_roll_hold/sideslip", "Lateral Coupling Beta", "deg"},
        {"preset/px4_roll_hold/lateral_controls",
            "Lateral Controls",
            "normalized"},
        {"preset/px4_roll_hold/saturation_status",
            "Saturation Status",
            "0 / 1"},
    }};

constexpr const TelemetryPlotBinding &GetTelemetryPlotBinding(
    DefaultTelemetryPlot plot) {
  return TelemetryPlotBindings[static_cast<std::size_t>(plot)];
}

enum class MonitorPreset {
  RollHold,
  Px4RollHoldDiagnostics,
  Count,
};

enum class MonitorPresetCategory {
  Controllers,
};

struct MonitorPresetCategoryDefinition {
  MonitorPresetCategory category;
  std::string_view name;
};

constexpr std::array MonitorPresetCategoryDefinitions{
    MonitorPresetCategoryDefinition{MonitorPresetCategory::Controllers,
        "Controllers"},
};

struct MonitorPresetDefinition {
  MonitorPreset preset;
  MonitorPresetCategory category;
  std::string_view name;
  std::array<DefaultTelemetryPlot, MaximumPresetPlots> requiredPlots;
  std::size_t requiredPlotCount;
};

constexpr std::array<MonitorPresetDefinition,
    static_cast<std::size_t>(MonitorPreset::Count)>
    MonitorPresetDefinitions{{
        {MonitorPreset::RollHold,
            MonitorPresetCategory::Controllers,
            "Roll Hold",
            {DefaultTelemetryPlot::RollHoldRollTracking,
                DefaultTelemetryPlot::RollHoldRollRateTracking,
                DefaultTelemetryPlot::RollHoldControlOutput},
            3},
        {MonitorPreset::Px4RollHoldDiagnostics,
            MonitorPresetCategory::Controllers,
            "PX4 Roll Hold Diagnostics",
            {DefaultTelemetryPlot::RollHoldRollTracking,
                DefaultTelemetryPlot::RollHoldRollError,
                DefaultTelemetryPlot::RollHoldRollRateTracking,
                DefaultTelemetryPlot::RollHoldRollRateError,
                DefaultTelemetryPlot::Px4RollHoldRateControllerTerms,
                DefaultTelemetryPlot::RollHoldControlOutput,
                DefaultTelemetryPlot::Px4RollHoldIntegrator,
                DefaultTelemetryPlot::Px4RollHoldCalibratedAirspeed,
                DefaultTelemetryPlot::Px4RollHoldAirspeedScaling,
                DefaultTelemetryPlot::Px4RollHoldLateralRates,
                DefaultTelemetryPlot::Px4RollHoldSideslip,
                DefaultTelemetryPlot::Px4RollHoldLateralControls,
                DefaultTelemetryPlot::Px4RollHoldSaturationStatus},
            13},
    }};

constexpr std::uint32_t GetPresetBit(MonitorPreset preset) {
  return std::uint32_t{1} << static_cast<std::size_t>(preset);
}

enum class PaneSelectionAction {
  NoChange,
  SelectAll,
  SelectNone,
};

PaneSelectionAction DrawPaneHeader(const char *title) {
  PaneSelectionAction action = PaneSelectionAction::NoChange;
  const UI::UIElement toolbar =
      UI::Toolbar()
          .Id(title)
          .Compact()
          .Height(26.0F)
          .Left(UI::Text(title))
          .Right(UI::HorizontalLayout().Spacing(
              4.0F)[+UI::Button("All").OnAction([&action] {
            action = PaneSelectionAction::SelectAll;
          }) + UI::Button("None").OnAction([&action] {
            action = PaneSelectionAction::SelectNone;
          })]);
  toolbar.Render();
  return action;
}

bool DrawVisibilityCheckbox(const char *label, bool &manualVisible,
    bool presetOnlyVisible) {
  bool displayedValue = manualVisible;
  const bool changed = ImGui::Checkbox(label, &displayedValue);
  if (changed) {
    manualVisible = displayedValue;
  } else if (presetOnlyVisible) {
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    const float inset = (itemMax.y - itemMin.y) * 0.28F;
    const float centerY = (itemMin.y + itemMax.y) * 0.5F;
    const float halfThickness = std::max(1.0F, UI::Ui(1.0F));
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(itemMin.x + inset, centerY - halfThickness),
        ImVec2(itemMax.x - inset, centerY + halfThickness),
        ImGui::GetColorU32(ImGuiCol_CheckMark),
        UI::Ui(1.0F));
  }
  return changed;
}

bool ContainsCaseInsensitive(std::string_view text, std::string_view query) {
  if (query.empty()) {
    return true;
  }

  const auto equalIgnoringCase = [](char left, char right) {
    const auto toLower = [](char value) {
      if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
      }
      return value;
    };
    return toLower(left) == toLower(right);
  };
  return std::search(text.begin(),
             text.end(),
             query.begin(),
             query.end(),
             equalIgnoringCase)
         != text.end();
}

double CalculateTimelineTickSpacing(double durationSec) {
  if (!std::isfinite(durationSec) || durationSec <= 0.0) {
    return 1.0;
  }

  const double rawSpacing =
      durationSec / static_cast<double>(TargetTimelineTickCount - 1);
  const double magnitude = std::pow(10.0, std::floor(std::log10(rawSpacing)));
  const double normalized = rawSpacing / magnitude;
  const double niceNormalized = normalized <= 1.0   ? 1.0
                                : normalized <= 2.0 ? 2.0
                                : normalized <= 5.0 ? 5.0
                                                    : 10.0;
  return niceNormalized * magnitude;
}

std::vector<double> CalculateTimelineTicks(double minSec, double maxSec) {
  std::vector<double> ticks;
  const double spacing = CalculateTimelineTickSpacing(maxSec - minSec);
  if (!std::isfinite(spacing) || spacing <= 0.0) {
    return ticks;
  }

  const double firstTick = std::ceil(minSec / spacing) * spacing;
  constexpr std::size_t MaximumTickCount = 64;
  for (double tick = firstTick;
      tick <= maxSec + spacing * 1.0e-6 && ticks.size() < MaximumTickCount;
      tick += spacing) {
    ticks.push_back(std::abs(tick) < spacing * 1.0e-9 ? 0.0 : tick);
  }
  return ticks;
}

void DrawVerticalPaneSplitter(const char *id, float height, float &sizeLogical,
    float minLogical, float maxLogical) {
  const float splitterWidth = UI::Ui(PaneSplitterThickness);
  const ImVec2 splitterMin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id,
      ImVec2(splitterWidth, std::max(height, 1.0F)),
      ImGuiButtonFlags_MouseButtonLeft);
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  if (hovered || active) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  }
  if (active) {
    const float uiScale = std::max(UI::GetUIScale(), 0.001F);
    sizeLogical =
        ClampToOrderedRange(sizeLogical + ImGui::GetIO().MouseDelta.x / uiScale,
            minLogical,
            maxLogical);
  }

  const ImU32 color = ImGui::GetColorU32(active    ? ImGuiCol_SeparatorActive
                                         : hovered ? ImGuiCol_SeparatorHovered
                                                   : ImGuiCol_Separator);
  const float centerX = splitterMin.x + splitterWidth * 0.5F;
  ImGui::GetWindowDrawList()->AddLine(ImVec2(centerX, splitterMin.y),
      ImVec2(centerX, splitterMin.y + height),
      color,
      active || hovered ? UI::Ui(2.0F) : UI::Ui(1.0F));
}

void DrawHorizontalPaneSplitter(const char *id, float width,
    float &bottomSizeLogical, float minLogical, float maxLogical) {
  const float splitterHeight = UI::Ui(PaneSplitterThickness);
  const ImVec2 splitterMin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id,
      ImVec2(std::max(width, 1.0F), splitterHeight),
      ImGuiButtonFlags_MouseButtonLeft);
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  if (hovered || active) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
  }
  if (active) {
    const float uiScale = std::max(UI::GetUIScale(), 0.001F);
    bottomSizeLogical = ClampToOrderedRange(
        bottomSizeLogical - ImGui::GetIO().MouseDelta.y / uiScale,
        minLogical,
        maxLogical);
  }

  const ImU32 color = ImGui::GetColorU32(active    ? ImGuiCol_SeparatorActive
                                         : hovered ? ImGuiCol_SeparatorHovered
                                                   : ImGuiCol_Separator);
  const float centerY = splitterMin.y + splitterHeight * 0.5F;
  ImGui::GetWindowDrawList()->AddLine(ImVec2(splitterMin.x, centerY),
      ImVec2(splitterMin.x + width, centerY),
      color,
      active || hovered ? UI::Ui(2.0F) : UI::Ui(1.0F));
}
} // namespace

MonitorView::MonitorView()
    : timelineModel_(renderState_.timeline),
      timelineViewRange_(timelineModel_.viewRange),
      visibleTimeRange_(timelineModel_.visibleRange),
      telemetryHistoryRange_(timelineModel_.historyRange),
      sharedXAxisTicks_(timelineModel_.sharedXAxisTicks),
      timelineViewWindowSec_(timelineModel_.viewWindowSec),
      liveWindowSec_(timelineModel_.liveWindowSec),
      selectedTimeSec_(timelineModel_.cursorTimeSec),
      liveView_(timelineModel_.live),
      selectedTimeInitialized_(timelineModel_.cursorInitialized),
      plots_(renderState_.plots), nextPlotId_(renderState_.nextPlotId),
      selectedPlotId_(renderState_.selectedPlotId),
      plotLayout_(renderState_.plotLayout),
      activePresetMask_(renderState_.activePresetMask),
      explorerPaneWidth_(renderState_.explorerPaneWidth),
      timelinePaneHeight_(renderState_.timelinePaneHeight),
      explorerPaneOpen_(renderState_.explorerPaneOpen),
      timelinePaneOpen_(renderState_.timelinePaneOpen),
      channelSearch_(renderState_.channelSearch),
      selectedChannelPath_(renderState_.selectedChannelPath),
      timelineDragMode_(renderState_.timelineDragMode),
      timelineDragTarget_(renderState_.timelineDragTarget),
      timelineDragInitialRange_(renderState_.timelineDragInitialRange),
      timelineDragAxisRange_(renderState_.timelineDragAxisRange),
      timelineDragAnchorSec_(renderState_.timelineDragAnchorSec),
      linearizationTrackSnapTimeSec_(
          renderState_.linearizationTrackSnapTimeSec),
      selectedDynamicModeIndex_(renderState_.selectedDynamicModeIndex),
      selectedDynamicModeSnapshotTimeSec_(
          renderState_.selectedDynamicModeSnapshotTimeSec) {}

void MonitorView::Render(const MonitorInput &input, const MonitorState &state,
    architecture::EventSink<MonitorEvent> events) {
  renderState_ = state;
  events_ = std::move(events);
  if (!renderState_.workspaceInitialized) {
    CreateDefaultPreset();
    renderState_.workspaceInitialized = true;
  }

  const TelemetrySources &sources = input;
  if (sources.primary == nullptr) {
    ImGui::TextDisabled("Primary telemetry is unavailable.");
    events_.Emit(MonitorStateChanged{renderState_});
    return;
  }

  const telemetry::TelemetrySnapshot &telemetry = *sources.primary;
  SynchronizeTimelineState(telemetry);

  const std::span dynamicModeHistory = input.dynamicModes.history;

  if (!ImGui::BeginTabBar("MonitorViews")) {
    events_.Emit(MonitorStateChanged{renderState_});
    return;
  }

  if (ImGui::BeginTabItem("Plots")) {
    DrawWindow(sources, dynamicModeHistory);
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem("Dynamic Modes")) {
    DrawDynamicModes(input.dynamicModes);
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
  events_.Emit(MonitorStateChanged{renderState_});
}

void MonitorView::DrawDynamicModes(
    const MonitorDynamicModeInput &dynamicModes) {
  if (!dynamicModes.available) {
    ImGui::TextDisabled(
        "Dynamic mode analysis is not available for this autopilot.");
    return;
  }

  bool automaticUpdates = dynamicModes.automaticUpdatesEnabled;
  if (ImGui::Checkbox("Automatic linearization", &automaticUpdates)) {
    events_.Emit(MonitorAutomaticLinearizationChanged{automaticUpdates});
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Run asynchronous aircraft linearization every 5 seconds");
  }
  ImGui::SameLine();

  const bool updateInProgress = dynamicModes.updateInProgress;
  const std::string_view updateError = dynamicModes.errorMessage;

  if (!automaticUpdates) {
    ImGui::TextDisabled(updateInProgress ? "Off (current worker is finishing)"
                                         : "Off (latest result retained)");
  } else if (updateInProgress) {
    ImGui::TextDisabled("Updating linearization asynchronously...");
  } else if (!updateError.empty()) {
    ImGui::TextColored(UI::GetDarkEditorSemanticColor(UI::SemanticColor::Error),
        "Latest linearization failed: %.*s",
        static_cast<int>(updateError.size()),
        updateError.data());
  }

  if (!selectedTimeInitialized_) {
    ImGui::Separator();
    ImGui::TextDisabled("Waiting for a Monitor timeline time.");
    return;
  }
  const gnc::DynamicModeSnapshot *snapshot =
      FindLatestDynamicModeAtOrBefore(dynamicModes.history, selectedTimeSec_);
  if (snapshot == nullptr) {
    selectedDynamicModeIndex_.reset();
    selectedDynamicModeSnapshotTimeSec_.reset();
    ImGui::Separator();
    ImGui::Text("Timeline time: %.3f s", selectedTimeSec_);
    ImGui::TextDisabled("No linearization available at this time.");
    return;
  }

  const gnc::DynamicModeAnalysis *analysis = &snapshot->analysis;
  if (!analysis->valid) {
    ImGui::Separator();
    ImGui::TextColored(UI::GetDarkEditorSemanticColor(UI::SemanticColor::Error),
        "Dynamic-mode analysis is unavailable: %s",
        analysis->errorMessage.c_str());
    return;
  }

  const double ageSec =
      std::max(0.0, selectedTimeSec_ - snapshot->simulationTimeSec);
  ImGui::TextDisabled("Timeline %.3f s  |  Linearization %.3f s  |  Age %.3f s",
      selectedTimeSec_,
      snapshot->simulationTimeSec,
      ageSec);
  ImGui::TextDisabled("Linearization: Valid  |  Full A: %zu modes",
      analysis->modes.size());
  ImGui::Separator();

  if (analysis->modes.empty()) {
    ImGui::TextDisabled("No dynamic modes were detected.");
    return;
  }
  if (!selectedDynamicModeSnapshotTimeSec_
      || *selectedDynamicModeSnapshotTimeSec_ != snapshot->simulationTimeSec) {
    selectedDynamicModeSnapshotTimeSec_ = snapshot->simulationTimeSec;
    selectedDynamicModeIndex_ = 0;
  } else if (!selectedDynamicModeIndex_
             || *selectedDynamicModeIndex_ >= analysis->modes.size()) {
    selectedDynamicModeIndex_ = 0;
  }

  constexpr ImGuiTableFlags ModeTableFlags =
      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
      | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("DynamicModeTable", 6, ModeTableFlags)) {
    ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthStretch, 1.3F);
    ImGui::TableSetupColumn("Eigenvalue",
        ImGuiTableColumnFlags_WidthStretch,
        1.5F);
    ImGui::TableSetupColumn("wn (rad/s)",
        ImGuiTableColumnFlags_WidthStretch,
        0.9F);
    ImGui::TableSetupColumn("zeta", ImGuiTableColumnFlags_WidthStretch, 0.7F);
    ImGui::TableSetupColumn("Period", ImGuiTableColumnFlags_WidthStretch, 0.8F);
    ImGui::TableSetupColumn("Stability",
        ImGuiTableColumnFlags_WidthStretch,
        0.9F);
    ImGui::TableHeadersRow();

    for (std::size_t modeIndex = 0; modeIndex < analysis->modes.size();
        ++modeIndex) {
      const gnc::DynamicMode &mode = analysis->modes[modeIndex];
      ImGui::PushID(static_cast<int>(modeIndex));
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      const std::string_view modeName = gnc::ToString(mode.classification);
      if (ImGui::Selectable(modeName.data(),
              selectedDynamicModeIndex_ == modeIndex,
              ImGuiSelectableFlags_SpanAllColumns)) {
        selectedDynamicModeIndex_ = modeIndex;
      }

      ImGui::TableSetColumnIndex(1);
      if (std::abs(mode.eigenvalue.imag()) > 0.0) {
        ImGui::Text("%.3f \xC2\xB1 %.3fi",
            mode.eigenvalue.real(),
            std::abs(mode.eigenvalue.imag()));
      } else {
        ImGui::Text("%.3f", mode.eigenvalue.real());
      }
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%.3f", mode.naturalFrequencyRadPerSec);
      ImGui::TableSetColumnIndex(3);
      if (mode.dampingRatio) {
        ImGui::Text("%.3f", *mode.dampingRatio);
      } else {
        ImGui::TextDisabled("--");
      }
      ImGui::TableSetColumnIndex(4);
      if (mode.periodSec) {
        ImGui::Text("%.3f s", *mode.periodSec);
      } else {
        ImGui::TextDisabled("--");
      }
      ImGui::TableSetColumnIndex(5);
      const UI::SemanticColor stabilityColor =
          mode.stability == gnc::DynamicModeStability::Stable
              ? UI::SemanticColor::Success
          : mode.stability == gnc::DynamicModeStability::Unstable
              ? UI::SemanticColor::Error
              : UI::SemanticColor::Warning;
      const std::string_view stability = gnc::ToString(mode.stability);
      ImGui::TextColored(UI::GetDarkEditorSemanticColor(stabilityColor),
          "%.*s",
          static_cast<int>(stability.size()),
          stability.data());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  const gnc::DynamicMode &selectedMode =
      analysis->modes[*selectedDynamicModeIndex_];
  const std::string_view selectedModeName =
      gnc::ToString(selectedMode.classification);
  ImGui::Spacing();
  ImGui::SeparatorText("Dominant States");
  ImGui::Text("%.*s",
      static_cast<int>(selectedModeName.size()),
      selectedModeName.data());

  constexpr ImGuiTableFlags ParticipationTableFlags =
      ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH
      | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("DynamicModeParticipation",
          2,
          ParticipationTableFlags)) {
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableSetupColumn("Normalized participation",
        ImGuiTableColumnFlags_WidthStretch,
        2.0F);
    ImGui::TableHeadersRow();

    std::size_t displayedCount = 0;
    for (const gnc::DynamicModeStateParticipation &state :
        selectedMode.stateParticipations) {
      if (displayedCount >= MaximumDisplayedModeStates
          || (displayedCount > 0
              && state.normalizedMagnitude < MinimumDisplayedParticipation)) {
        break;
      }
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(state.stateName.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%.2f", state.normalizedMagnitude);
      ++displayedCount;
    }
    ImGui::EndTable();
  }
}

void MonitorView::CreateDefaultPreset() {
  plots_.clear();
  nextPlotId_ = 1;

  const auto addPresetPlot = [this](DefaultTelemetryPlot preset,
                                 std::initializer_list<std::string_view>
                                     paths) {
    const TelemetryPlotBinding &binding = GetTelemetryPlotBinding(preset);
    MonitorPlot &plot = CreatePlot(std::string(binding.plotTitle),
        std::string(binding.nodePath),
        std::string(binding.yAxisLabel));
    for (const std::string_view path : paths) {
      SetChannelEnabled(plot, path, true);
    }
  };

  addPresetPlot(DefaultTelemetryPlot::AerodynamicAngles,
      {telemetry::paths::AircraftAeroAlpha,
          telemetry::paths::AircraftAeroBeta});
  addPresetPlot(DefaultTelemetryPlot::Attitude,
      {telemetry::paths::AircraftAttitudeRoll,
          telemetry::paths::AircraftAttitudePitch,
          telemetry::paths::AircraftAttitudeHeading});
  addPresetPlot(DefaultTelemetryPlot::BodyVelocities,
      {telemetry::paths::AircraftBodyVelocityU,
          telemetry::paths::AircraftBodyVelocityV,
          telemetry::paths::AircraftBodyVelocityW});
  addPresetPlot(DefaultTelemetryPlot::BodyRates,
      {telemetry::paths::AircraftRateP,
          telemetry::paths::AircraftRateQ,
          telemetry::paths::AircraftRateR});
  addPresetPlot(DefaultTelemetryPlot::Airspeed,
      {telemetry::paths::AircraftCalibratedAirspeed,
          telemetry::paths::AircraftTrueAirspeed});
  addPresetPlot(DefaultTelemetryPlot::AltitudeAgl,
      {telemetry::paths::AircraftAltitudeAgl});
  addPresetPlot(DefaultTelemetryPlot::BodyAccelerations,
      {telemetry::paths::AircraftBodyAccelerationU,
          telemetry::paths::AircraftBodyAccelerationV,
          telemetry::paths::AircraftBodyAccelerationW});
  addPresetPlot(DefaultTelemetryPlot::AngularAccelerations,
      {telemetry::paths::AircraftAngularAccelerationP,
          telemetry::paths::AircraftAngularAccelerationQ,
          telemetry::paths::AircraftAngularAccelerationR});

  const auto addHiddenPresetPlot = [this](DefaultTelemetryPlot preset,
                                       std::initializer_list<std::string_view>
                                           paths) {
    const TelemetryPlotBinding &binding = GetTelemetryPlotBinding(preset);
    MonitorPlot &plot = CreatePlot(std::string(binding.plotTitle),
        std::string(binding.nodePath),
        std::string(binding.yAxisLabel));
    plot.manualVisible = false;
    for (const std::string_view path : paths) {
      SetChannelEnabled(plot, path, true);
    }
  };

  addHiddenPresetPlot(DefaultTelemetryPlot::RollHoldRollTracking,
      {telemetry::paths::AutopilotRollHoldCommandedRoll,
          telemetry::paths::AutopilotRollHoldRoll});
  addHiddenPresetPlot(DefaultTelemetryPlot::RollHoldRollError,
      {telemetry::paths::AutopilotRollHoldRollError});
  addHiddenPresetPlot(DefaultTelemetryPlot::RollHoldRollRateTracking,
      {telemetry::paths::AutopilotRollHoldCommandedRollRate,
          telemetry::paths::AutopilotRollHoldRollRate});
  addHiddenPresetPlot(DefaultTelemetryPlot::RollHoldRollRateError,
      {telemetry::paths::AutopilotRollHoldRollRateError});
  addHiddenPresetPlot(DefaultTelemetryPlot::Px4RollHoldRateControllerTerms,
      {telemetry::paths::AutopilotRollHoldRateProportionalTerm,
          telemetry::paths::AutopilotRollHoldRateIntegralTerm,
          telemetry::paths::AutopilotRollHoldRateDerivativeTerm,
          telemetry::paths::AutopilotRollHoldRateFeedForwardTerm});
  addHiddenPresetPlot(DefaultTelemetryPlot::RollHoldControlOutput,
      {telemetry::paths::AutopilotRollHoldUnscaledTorqueCommand,
          telemetry::paths::AutopilotRollHoldRawTorqueCommand,
          telemetry::paths::AutopilotRollHoldRollTorqueCommand,
          telemetry::paths::AutopilotRollHoldAileronCommand});
  addHiddenPresetPlot(DefaultTelemetryPlot::Px4RollHoldIntegrator,
      {telemetry::paths::AutopilotRollHoldRateIntegralTerm,
          telemetry::paths::AutopilotRollHoldRateIntegratorPositiveLimit,
          telemetry::paths::AutopilotRollHoldRateIntegratorNegativeLimit});
  addHiddenPresetPlot(DefaultTelemetryPlot::Px4RollHoldCalibratedAirspeed,
      {telemetry::paths::AircraftCalibratedAirspeed});
  addHiddenPresetPlot(DefaultTelemetryPlot::Px4RollHoldAirspeedScaling,
      {telemetry::paths::AutopilotRollHoldAirspeedScaling});
  addHiddenPresetPlot(DefaultTelemetryPlot::Px4RollHoldLateralRates,
      {telemetry::paths::AircraftRateP, telemetry::paths::AircraftRateR});
  addHiddenPresetPlot(DefaultTelemetryPlot::Px4RollHoldSideslip,
      {telemetry::paths::AircraftAeroBeta});
  addHiddenPresetPlot(DefaultTelemetryPlot::Px4RollHoldLateralControls,
      {telemetry::paths::AircraftControlAileron,
          telemetry::paths::AircraftControlRudder});
  addHiddenPresetPlot(DefaultTelemetryPlot::Px4RollHoldSaturationStatus,
      {telemetry::paths::AutopilotRollHoldPositiveSaturation,
          telemetry::paths::AutopilotRollHoldNegativeSaturation,
          telemetry::paths::AutopilotRollHoldIntegratorLimited});

  selectedPlotId_ = plots_.empty() ? 0 : plots_.front().id;
}

MonitorView::MonitorPlot &MonitorView::CreatePlot(std::string title,
    std::string telemetryGroupPath, std::string yAxisLabel) {
  plots_.push_back(MonitorPlot{nextPlotId_++,
      std::move(title),
      {},
      std::move(telemetryGroupPath),
      std::move(yAxisLabel),
      true});
  selectedPlotId_ = plots_.back().id;
  return plots_.back();
}

MonitorView::MonitorPlot *MonitorView::FindBoundPlot(
    std::string_view telemetryNodePath) {
  const auto plot = std::find_if(plots_.begin(),
      plots_.end(),
      [telemetryNodePath](const MonitorPlot &candidate) {
        return candidate.telemetryGroupPath == telemetryNodePath;
      });
  return plot == plots_.end() ? nullptr : &*plot;
}

void MonitorView::DeletePlot(std::uint64_t plotId) {
  const auto plot = std::find_if(plots_.begin(),
      plots_.end(),
      [plotId](
          const MonitorPlot &candidate) { return candidate.id == plotId; });
  if (plot == plots_.end()) {
    return;
  }

  plots_.erase(plot);
  if (selectedPlotId_ == plotId) {
    selectedPlotId_ = plots_.empty() ? 0 : plots_.front().id;
  }
}

void MonitorView::SetChannelEnabled(MonitorPlot &plot,
    std::string_view channelPath, bool enabled) {
  const auto channel =
      std::find(plot.channels.begin(), plot.channels.end(), channelPath);
  if (enabled) {
    if (channel == plot.channels.end()) {
      plot.channels.emplace_back(channelPath);
    }
    return;
  }

  if (channel != plot.channels.end()) {
    plot.channels.erase(channel);
  }
}

void MonitorView::DrawWindow(const TelemetrySources &sources,
    std::span<const gnc::DynamicModeSnapshot> dynamicModeHistory) {
  const telemetry::TelemetrySnapshot &telemetry = *sources.primary;
  if (!selectedChannelPath_.empty()
      && telemetry.Find(selectedChannelPath_) == nullptr) {
    selectedChannelPath_.clear();
  }

  const ImVec2 workspaceSize = ImGui::GetContentRegionAvail();
  const float uiScale = std::max(UI::GetUIScale(), 0.001F);
  if (explorerPaneOpen_) {
    const float availableWidthLogical = workspaceSize.x / uiScale;
    const float maximumExplorerWidth = std::max(ExplorerMinWidth,
        std::min(ExplorerMaxWidth,
            availableWidthLogical - ExplorerMinimumPlotWidth
                - PaneSplitterThickness));
    explorerPaneWidth_ = ClampToOrderedRange(explorerPaneWidth_,
        ExplorerMinWidth,
        maximumExplorerWidth);

    if (ImGui::BeginChild("ExplorerPane",
            ImVec2(UI::Ui(explorerPaneWidth_), 0.0F),
            false,
            ImGuiWindowFlags_NoScrollbar
                | ImGuiWindowFlags_NoScrollWithMouse)) {
      DrawExplorerHeader();
      DrawTelemetryBrowser(telemetry);
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0F, 0.0F);
    DrawVerticalPaneSplitter("##ExplorerSplitter",
        workspaceSize.y,
        explorerPaneWidth_,
        ExplorerMinWidth,
        maximumExplorerWidth);
  } else {
    if (ImGui::BeginChild("ExplorerPaneCollapsed",
            ImVec2(UI::Ui(ExplorerCollapsedWidth), 0.0F),
            true,
            ImGuiWindowFlags_NoScrollbar
                | ImGuiWindowFlags_NoScrollWithMouse)) {
      if (ImGui::Button(">##OpenExplorer", ImVec2(-1.0F, 0.0F))) {
        explorerPaneOpen_ = true;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Open Telemetry / Presets");
      }
    }
    ImGui::EndChild();
  }

  ImGui::SameLine(0.0F, 0.0F);
  if (ImGui::BeginChild("MonitorPlotPane",
          ImVec2(0.0F, 0.0F),
          false,
          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    DrawPlotWorkspace(sources, dynamicModeHistory);
  }
  ImGui::EndChild();
}

void MonitorView::DrawExplorerHeader() {
  if (ImGui::Button("<##CloseExplorer")) {
    explorerPaneOpen_ = false;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Collapse Telemetry / Presets");
  }
  ImGui::SameLine();
  ImGui::TextUnformatted("Telemetry / Presets");
}

void MonitorView::DrawToolbar(const telemetry::TelemetrySnapshot &telemetry) {
  if (ImGui::Button("+ Plot")) {
    CreatePlot("Plot " + std::to_string(nextPlotId_));
  }
  ImGui::SameLine();

  bool live = liveView_;
  if (ImGui::Checkbox("Live##Toolbar", &live)) {
    SetLiveView(live);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(liveView_
                          ? "Following the latest telemetry time"
                          : "Viewport paused; telemetry continues recording");
  }
  ImGui::SameLine();

  DrawPlotLayoutSelector();

  ImGui::SameLine();
  const std::size_t channelCount = telemetry.GetChannelPaths().size();
  ImGui::TextDisabled("%zu channels | %.2f - %.2f s%s",
      channelCount,
      visibleTimeRange_.minSec,
      visibleTimeRange_.maxSec,
      liveView_ ? " | following latest" : " | view paused");
}

void MonitorView::DrawPlotLayoutSelector() {
  ImGui::TextUnformatted("Layout:");
  const auto drawLayoutButton = [this](const char *label,
                                    MonitorPlotLayout layout) {
    ImGui::SameLine();
    const bool isSelected = plotLayout_ == layout;
    if (isSelected) {
      ImGui::PushStyleColor(ImGuiCol_Button,
          ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if (ImGui::Button(label)) {
      plotLayout_ = layout;
    }
    if (isSelected) {
      ImGui::PopStyleColor();
    }
  };

  drawLayoutButton("List", MonitorPlotLayout::List);
  drawLayoutButton("2x2", MonitorPlotLayout::Grid2x2);
  drawLayoutButton("3x3", MonitorPlotLayout::Grid3x3);
}

void MonitorView::DrawTelemetryBrowser(
    const telemetry::TelemetrySnapshot &telemetry) {
  constexpr ImGuiWindowFlags OuterContainerFlags =
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (!ImGui::BeginChild("TelemetryBrowser",
          ImVec2(0.0F, 0.0F),
          true,
          OuterContainerFlags)) {
    ImGui::EndChild();
    return;
  }

  const ImGuiStyle &style = ImGui::GetStyle();
  const float availableHeight = ImGui::GetContentRegionAvail().y;
  const float dividerHeight =
      style.ItemSpacing.y * 2.0F + std::max(style.SeparatorSize, 1.0F);
  const float paneHeight =
      std::max(0.0F, (availableHeight - dividerHeight) * 0.5F);

  if (ImGui::BeginChild("TelemetryPane",
          ImVec2(0.0F, paneHeight),
          false,
          ImGuiWindowFlags_HorizontalScrollbar)) {
    const PaneSelectionAction selectionAction = DrawPaneHeader("Telemetry");
    if (selectionAction != PaneSelectionAction::NoChange) {
      const bool visible = selectionAction == PaneSelectionAction::SelectAll;
      for (MonitorPlot &plot : plots_) {
        if (!plot.telemetryGroupPath.empty()) {
          plot.manualVisible = visible;
        }
      }
    }
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##TelemetrySearch",
        "Search channels...",
        channelSearch_.data(),
        channelSearch_.size());

    BrowserNode root;
    const std::string_view search(channelSearch_.data());
    for (const std::string_view path : telemetry.GetChannelPaths()) {
      if (ContainsCaseInsensitive(path, search)) {
        AddBrowserPath(root, path);
      }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing,
        UI::Ui(TelemetryTreeIndentSpacing));
    if (root.children.empty()) {
      ImGui::TextDisabled(search.empty() ? "Waiting for telemetry channels."
                                         : "No matching channels.");
    } else {
      for (const auto &[name, child] : root.children) {
        (void)name;
        DrawBrowserNode(child, !search.empty());
      }
    }
    ImGui::PopStyleVar();
  }
  ImGui::EndChild();

  ImGui::Separator();
  if (ImGui::BeginChild("PresetPane", ImVec2(0.0F, 0.0F), false)) {
    DrawPresetPanel();
  }
  ImGui::EndChild();

  ImGui::EndChild();
}

void MonitorView::DrawPresetPanel() {
  const PaneSelectionAction selectionAction = DrawPaneHeader("Presets");
  if (selectionAction == PaneSelectionAction::SelectAll) {
    activePresetMask_ = 0;
    for (const MonitorPresetDefinition &preset : MonitorPresetDefinitions) {
      activePresetMask_ |= GetPresetBit(preset.preset);
    }
  } else if (selectionAction == PaneSelectionAction::SelectNone) {
    activePresetMask_ = 0;
  }

  constexpr ImGuiTreeNodeFlags CategoryFlags =
      ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow
      | ImGuiTreeNodeFlags_SpanAvailWidth;

  for (const MonitorPresetCategoryDefinition &category :
      MonitorPresetCategoryDefinitions) {
    ImGui::PushID(static_cast<int>(category.category));
    const bool isOpen = ImGui::TreeNodeEx(category.name.data(), CategoryFlags);
    if (isOpen) {
      for (std::size_t presetIndex = 0;
          presetIndex < MonitorPresetDefinitions.size();
          ++presetIndex) {
        const MonitorPresetDefinition &preset =
            MonitorPresetDefinitions[presetIndex];
        if (preset.category != category.category) {
          continue;
        }

        bool active = IsPresetActive(presetIndex);
        ImGui::PushID(static_cast<int>(presetIndex));
        if (ImGui::Checkbox(preset.name.data(), &active)) {
          SetPresetActive(presetIndex, active);
        }
        ImGui::PopID();
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
}

void MonitorView::AddBrowserPath(BrowserNode &root,
    std::string_view path) const {
  BrowserNode *node = &root;
  std::string currentPath;
  std::size_t segmentStart = 0;
  while (segmentStart < path.size()) {
    const std::size_t separator = path.find('/', segmentStart);
    const std::size_t segmentEnd =
        separator == std::string_view::npos ? path.size() : separator;
    const std::string segment(
        path.substr(segmentStart, segmentEnd - segmentStart));
    if (!currentPath.empty()) {
      currentPath.push_back('/');
    }
    currentPath += segment;

    auto [child, inserted] = node->children.try_emplace(segment);
    if (inserted) {
      child->second.name = segment;
      child->second.fullPath = currentPath;
    }
    node = &child->second;

    if (separator == std::string_view::npos) {
      break;
    }
    segmentStart = separator + 1;
  }
  node->isChannel = true;
}

void MonitorView::DrawBrowserNode(const BrowserNode &node, bool expandAll) {
  if (node.isChannel && node.children.empty()) {
    DrawBrowserChannel(node.name, node.fullPath);
    return;
  }

  ImGui::PushID(node.fullPath.c_str());
  if (expandAll) {
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  }
  constexpr ImGuiTreeNodeFlags FoldoutFlags =
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  const bool isOpen = ImGui::TreeNodeEx("##Foldout", FoldoutFlags);
  ImGui::SameLine(0.0F, UI::Ui(TelemetryTreeControlSpacing));

  if (MonitorPlot *plot = FindBoundPlot(node.fullPath)) {
    bool manualVisible = plot->manualVisible;
    const bool presetOnlyVisible =
        !manualVisible && IsPlotVisibleByPreset(*plot);
    if (DrawVisibilityCheckbox("##PlotVisible",
            manualVisible,
            presetOnlyVisible)) {
      plot->manualVisible = manualVisible;
    }
    ImGui::SameLine(0.0F, UI::Ui(TelemetryTreeControlSpacing));
  }
  ImGui::TextUnformatted(node.name.c_str());
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", node.fullPath.c_str());
  }

  if (isOpen) {
    ImGui::TreePush("##Children");
    for (const auto &[name, child] : node.children) {
      (void)name;
      DrawBrowserNode(child, expandAll);
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}

void MonitorView::DrawBrowserChannel(std::string_view label,
    std::string_view channelPath) {
  const std::string channelId(channelPath);
  ImGui::PushID(channelId.c_str());
  const bool isSelected = selectedChannelPath_ == channelPath;
  const std::string channelLabel(GetTelemetryDisplayName(label));
  if (ImGui::Selectable(channelLabel.c_str(),
          isSelected,
          ImGuiSelectableFlags_None,
          ImVec2(0.0F, 0.0F))) {
    selectedChannelPath_ = channelPath;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", channelId.c_str());
  }
  ImGui::PopID();
}

void MonitorView::DrawPlotWorkspace(const TelemetrySources &sources,
    std::span<const gnc::DynamicModeSnapshot> dynamicModeHistory) {
  const telemetry::TelemetrySnapshot &telemetry = *sources.primary;
  DrawToolbar(telemetry);
  ImGui::Separator();

  const ImVec2 availableSize = ImGui::GetContentRegionAvail();
  const ImGuiStyle &style = ImGui::GetStyle();
  float plotRegionHeight = availableSize.y;
  float timelineRegionHeight = UI::Ui(TimelineCollapsedHeight);
  float maximumTimelineHeight = TimelineMinHeight;
  if (timelinePaneOpen_) {
    const float uiScale = std::max(UI::GetUIScale(), 0.001F);
    maximumTimelineHeight = std::max(TimelineMinHeight,
        std::min(TimelineMaxHeight, availableSize.y / uiScale * 0.5F));
    timelinePaneHeight_ = ClampToOrderedRange(timelinePaneHeight_,
        TimelineMinHeight,
        maximumTimelineHeight);
    timelineRegionHeight = UI::Ui(timelinePaneHeight_);
    plotRegionHeight = std::max(1.0F,
        availableSize.y - timelineRegionHeight - UI::Ui(PaneSplitterThickness)
            - style.ItemSpacing.y * 2.0F);
  } else {
    plotRegionHeight = std::max(1.0F,
        availableSize.y - timelineRegionHeight - style.ItemSpacing.y);
  }

  if (ImGui::BeginChild("PlotScrollRegion",
          ImVec2(0.0F, plotRegionHeight),
          true,
          ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
    DrawPlotScrollRegion(sources);
  }
  ImGui::EndChild();

  if (timelinePaneOpen_) {
    DrawHorizontalPaneSplitter("##TimelineSplitter",
        availableSize.x,
        timelinePaneHeight_,
        TimelineMinHeight,
        maximumTimelineHeight);
  }

  constexpr ImGuiWindowFlags TimelineRegionFlags =
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (ImGui::BeginChild("TimelineRegion",
          ImVec2(0.0F, 0.0F),
          true,
          TimelineRegionFlags)) {
    DrawTimelineHeader();
    if (timelinePaneOpen_) {
      DrawTimeline(dynamicModeHistory);
    }
  }
  ImGui::EndChild();
}

void MonitorView::DrawPlotScrollRegion(const TelemetrySources &sources) {
  if (plotLayout_ == MonitorPlotLayout::List) {
    DrawPlotList(sources);
  } else if (plotLayout_ == MonitorPlotLayout::Grid2x2) {
    DrawPlotGrid(sources, 2);
  } else {
    DrawPlotGrid(sources, 3);
  }
}

void MonitorView::DrawTimelineHeader() {
  const bool wasOpen = timelinePaneOpen_;
  bool live = liveView_;
  const UI::UIElement toolbar =
      UI::Toolbar()
          .Id("TimelineHeader")
          .Compact()
          .Height(26.0F)
          .Left(UI::HorizontalLayout().Spacing(
              4.0F)[+UI::Button(
                        wasOpen ? "v##CollapseTimeline" : "^##OpenTimeline")
                        .Tooltip(
                            wasOpen ? "Collapse Timeline" : "Open Timeline")
                        .OnAction(
                            [this, wasOpen] { timelinePaneOpen_ = !wasOpen; })
                    + UI::Text("Timeline")])
          .Right(UI::Toggle("Live##Timeline", live)
                  .OnChanged([this](bool enabled) { SetLiveView(enabled); }));
  toolbar.Render();
}

void MonitorView::DrawTimeline(
    std::span<const gnc::DynamicModeSnapshot> dynamicModeHistory) {
  if (selectedTimeInitialized_) {
    ImGui::Text("View %.2f - %.2f s  |  Plot %.2f - %.2f s  |  Selected %.2f s",
        timelineViewRange_.minSec,
        timelineViewRange_.maxSec,
        visibleTimeRange_.minSec,
        visibleTimeRange_.maxSec,
        selectedTimeSec_);
  } else {
    ImGui::Text("View %.2f - %.2f s  |  Plot %.2f - %.2f s",
        timelineViewRange_.minSec,
        timelineViewRange_.maxSec,
        visibleTimeRange_.minSec,
        visibleTimeRange_.maxSec);
  }

  if (!telemetryHistoryRange_) {
    ImGui::TextDisabled("Waiting for telemetry history.");
    return;
  }

  DrawTimelineOverview(*telemetryHistoryRange_);
  ImGui::Dummy(ImVec2(0.0F, UI::Ui(TimelineRowSpacing)));
  DrawTimelineDetail();
  ImGui::Dummy(ImVec2(0.0F, UI::Ui(TimelineRowSpacing)));
  DrawLinearizationTrack(dynamicModeHistory);
}

void MonitorView::DrawTimelineOverview(const TimelineRange &historyRange) {
  ImGui::TextDisabled("Overview  History %.2f - %.2f s",
      historyRange.minSec,
      historyRange.maxSec);

  const TimelineRange trackRange = GetEffectiveHistoryRange(historyRange);

  const float barWidth = std::max(UI::Ui(80.0F),
      ImGui::GetContentRegionAvail().x
          - UI::Ui(TimelineHorizontalPadding) * 2.0F);
  const float barHeight = UI::Ui(TimelineOverviewBarHeight);
  const ImVec2 cursorPosition = ImGui::GetCursorScreenPos();
  const ImVec2 barMin(cursorPosition.x + UI::Ui(TimelineHorizontalPadding),
      cursorPosition.y);
  const ImVec2 barMax(barMin.x + barWidth, barMin.y + barHeight);
  const double historyDuration = trackRange.maxSec - trackRange.minSec;

  const auto timeToX = [&](double timeSec) {
    const double ratio =
        ClampToOrderedRange((timeSec - trackRange.minSec) / historyDuration,
            0.0,
            1.0);
    return barMin.x + static_cast<float>(ratio) * barWidth;
  };
  const auto xToTime = [&](float x) {
    const double ratio =
        ClampToOrderedRange(static_cast<double>((x - barMin.x) / barWidth),
            0.0,
            1.0);
    return trackRange.minSec + ratio * historyDuration;
  };

  const ImGuiIO &io = ImGui::GetIO();
  const bool barHovered = io.MousePos.x >= barMin.x && io.MousePos.x <= barMax.x
                          && io.MousePos.y >= barMin.y
                          && io.MousePos.y <= barMax.y;
  if (barHovered && io.KeyCtrl && io.MouseWheel != 0.0F
      && timelineDragMode_ == TimelineDragMode::None) {
    ZoomTimelineView(io.MouseWheel, xToTime(io.MousePos.x));
  }

  ImDrawList *drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(barMin,
      barMax,
      ImGui::GetColorU32(ImGuiCol_FrameBg),
      UI::Ui(3.0F));

  const float selectionMinX = timeToX(timelineViewRange_.minSec);
  const float selectionMaxX = timeToX(timelineViewRange_.maxSec);
  drawList->AddRectFilled(ImVec2(selectionMinX, barMin.y),
      ImVec2(selectionMaxX, barMax.y),
      ImGui::GetColorU32(ImGuiCol_FrameBgHovered),
      UI::Ui(3.0F));

  const float handleWidth = UI::Ui(TimelineHandleWidth);
  const ImU32 viewHandleColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  drawList->AddLine(ImVec2(selectionMinX, barMin.y - UI::Ui(2.0F)),
      ImVec2(selectionMinX, barMax.y + UI::Ui(2.0F)),
      viewHandleColor,
      UI::Ui(1.5F));
  drawList->AddLine(ImVec2(selectionMaxX, barMin.y - UI::Ui(2.0F)),
      ImVec2(selectionMaxX, barMax.y + UI::Ui(2.0F)),
      viewHandleColor,
      UI::Ui(1.5F));

  ImGui::SetCursorScreenPos(ImVec2(barMin.x - handleWidth * 0.5F, barMin.y));
  ImGui::InvisibleButton("##TimelineOverviewInteraction",
      ImVec2(barWidth + handleWidth, barHeight),
      ImGuiButtonFlags_MouseButtonLeft);
  const bool isHovered = ImGui::IsItemHovered();
  const bool isActive = ImGui::IsItemActive();
  const float mouseX = ImGui::GetIO().MousePos.x;
  const float handleHitRadius = handleWidth;
  const float startHandleDistance = std::abs(mouseX - selectionMinX);
  const float endHandleDistance = std::abs(mouseX - selectionMaxX);
  const bool isHandleHovered =
      std::min(startHandleDistance, endHandleDistance) <= handleHitRadius;
  const bool isSelectionHovered =
      mouseX > selectionMinX && mouseX < selectionMaxX;
  if (isHovered || isActive) {
    ImGui::SetMouseCursor(isHandleHovered      ? ImGuiMouseCursor_ResizeEW
                          : isSelectionHovered ? ImGuiMouseCursor_ResizeAll
                                               : ImGuiMouseCursor_Arrow);
  }

  if (ImGui::IsItemActivated()) {
    if (isHandleHovered) {
      timelineDragMode_ = startHandleDistance <= endHandleDistance
                              ? TimelineDragMode::Start
                              : TimelineDragMode::End;
    } else if (isSelectionHovered) {
      timelineDragMode_ = TimelineDragMode::Window;
    } else {
      timelineDragMode_ = TimelineDragMode::Window;
    }
    timelineDragTarget_ = TimelineDragTarget::TimelineView;
    timelineDragAnchorSec_ = xToTime(mouseX);
    timelineDragInitialRange_ = timelineViewRange_;
    timelineDragAxisRange_ = trackRange;
    SetLiveView(false);
  }

  if (isActive && timelineDragTarget_ == TimelineDragTarget::TimelineView) {
    const double mouseRatio = ClampToOrderedRange(
        static_cast<double>((ImGui::GetIO().MousePos.x - barMin.x) / barWidth),
        0.0,
        1.0);
    const double mouseTime =
        timelineDragAxisRange_.minSec
        + mouseRatio
              * (timelineDragAxisRange_.maxSec - timelineDragAxisRange_.minSec);
    const double minimumViewDuration = MinimumTimelineWindowSec;
    if (timelineDragMode_ == TimelineDragMode::Start) {
      const double maximumStartSec = std::max(trackRange.minSec,
          timelineViewRange_.maxSec - minimumViewDuration);
      timelineViewRange_.minSec =
          ClampToOrderedRange(mouseTime, trackRange.minSec, maximumStartSec);
    } else if (timelineDragMode_ == TimelineDragMode::End) {
      const double minimumEndSec = std::min(trackRange.maxSec,
          timelineViewRange_.minSec + minimumViewDuration);
      timelineViewRange_.maxSec =
          ClampToOrderedRange(mouseTime, minimumEndSec, trackRange.maxSec);
    } else {
      const double duration = ClampToOrderedRange(
          timelineDragInitialRange_.maxSec - timelineDragInitialRange_.minSec,
          MinimumTimelineWindowSec,
          historyDuration);
      const double desiredMin =
          timelineDragInitialRange_.minSec + mouseTime - timelineDragAnchorSec_;
      const double maximumMinSec =
          std::max(trackRange.minSec, trackRange.maxSec - duration);
      const double minSec =
          ClampToOrderedRange(desiredMin, trackRange.minSec, maximumMinSec);
      timelineViewRange_ = {minSec, minSec + duration};
    }
    ClampTimelineViewRangeToHistory();
    timelineViewWindowSec_ =
        timelineViewRange_.maxSec - timelineViewRange_.minSec;
  }
  if (ImGui::IsItemDeactivated()) {
    timelineDragMode_ = TimelineDragMode::None;
    timelineDragTarget_ = TimelineDragTarget::None;
  }
}

void MonitorView::DrawTimelineDetail() {
  ImGui::TextDisabled("Detail  View %.2f - %.2f s",
      timelineViewRange_.minSec,
      timelineViewRange_.maxSec);

  const TimelineRange detailRange = timelineViewRange_;
  const double viewDuration = detailRange.maxSec - detailRange.minSec;
  if (!std::isfinite(viewDuration) || viewDuration <= 0.0) {
    return;
  }

  const float barWidth = std::max(UI::Ui(80.0F),
      ImGui::GetContentRegionAvail().x
          - UI::Ui(TimelineHorizontalPadding) * 2.0F);
  const float barHeight = UI::Ui(TimelineDetailBarHeight);
  const ImVec2 cursorPosition = ImGui::GetCursorScreenPos();
  const ImVec2 barMin(cursorPosition.x + UI::Ui(TimelineHorizontalPadding),
      cursorPosition.y + ImGui::GetTextLineHeight());
  const ImVec2 barMax(barMin.x + barWidth, barMin.y + barHeight);

  const auto timeToX = [&](double timeSec) {
    const double ratio =
        ClampToOrderedRange((timeSec - detailRange.minSec) / viewDuration,
            0.0,
            1.0);
    return barMin.x + static_cast<float>(ratio) * barWidth;
  };
  const auto xToTime = [&](float x) {
    const double ratio =
        ClampToOrderedRange(static_cast<double>((x - barMin.x) / barWidth),
            0.0,
            1.0);
    return detailRange.minSec + ratio * viewDuration;
  };

  const ImGuiIO &io = ImGui::GetIO();
  const bool barHovered = io.MousePos.x >= barMin.x && io.MousePos.x <= barMax.x
                          && io.MousePos.y >= barMin.y
                          && io.MousePos.y <= barMax.y;
  if (barHovered && io.KeyCtrl && io.MouseWheel != 0.0F
      && timelineDragMode_ == TimelineDragMode::None) {
    ZoomTimelineView(io.MouseWheel, xToTime(io.MousePos.x));
  }

  ImDrawList *drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(barMin,
      barMax,
      ImGui::GetColorU32(ImGuiCol_FrameBg),
      UI::Ui(3.0F));
  const std::vector<double> ticks =
      CalculateTimelineTicks(detailRange.minSec, detailRange.maxSec);
  for (double tick : ticks) {
    const float tickX = timeToX(tick);
    drawList->AddLine(ImVec2(tickX, barMin.y),
        ImVec2(tickX, barMax.y),
        ImGui::GetColorU32(ImGuiCol_Border));
    char label[32]{};
    std::snprintf(label, sizeof(label), "%.3g s", tick);
    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    drawList->AddText(ImVec2(tickX - labelSize.x * 0.5F,
                          barMin.y - labelSize.y - UI::Ui(2.0F)),
        ImGui::GetColorU32(ImGuiCol_TextDisabled),
        label);
  }

  const float selectionMinX = timeToX(visibleTimeRange_.minSec);
  const float selectionMaxX = timeToX(visibleTimeRange_.maxSec);
  drawList->AddRectFilled(ImVec2(selectionMinX, barMin.y),
      ImVec2(selectionMaxX, barMax.y),
      ImGui::GetColorU32(ImGuiCol_SliderGrabActive),
      UI::Ui(3.0F));

  const float handleWidth = UI::Ui(TimelineHandleWidth);
  drawList->AddRectFilled(ImVec2(selectionMinX - handleWidth * 0.5F, barMin.y),
      ImVec2(selectionMinX + handleWidth * 0.5F, barMax.y),
      ImGui::GetColorU32(ImGuiCol_SliderGrab),
      UI::Ui(2.0F));
  drawList->AddRectFilled(ImVec2(selectionMaxX - handleWidth * 0.5F, barMin.y),
      ImVec2(selectionMaxX + handleWidth * 0.5F, barMax.y),
      ImGui::GetColorU32(ImGuiCol_SliderGrab),
      UI::Ui(2.0F));

  if (selectedTimeInitialized_ && selectedTimeSec_ >= detailRange.minSec
      && selectedTimeSec_ <= detailRange.maxSec) {
    const float cursorX = timeToX(selectedTimeSec_);
    drawList->AddLine(ImVec2(cursorX, barMin.y - UI::Ui(3.0F)),
        ImVec2(cursorX, barMax.y + UI::Ui(3.0F)),
        ImGui::GetColorU32(ImVec4(0.95F, 0.75F, 0.25F, 0.9F)),
        UI::Ui(1.5F));
  }

  ImGui::SetCursorScreenPos(ImVec2(barMin.x - handleWidth * 0.5F, barMin.y));
  ImGui::InvisibleButton("##TimelineDetailInteraction",
      ImVec2(barWidth + handleWidth, barHeight),
      ImGuiButtonFlags_MouseButtonLeft);
  const bool isHovered = ImGui::IsItemHovered();
  const bool isActive = ImGui::IsItemActive();
  const float mouseX = ImGui::GetIO().MousePos.x;
  const float handleHitRadius = handleWidth;
  const float startHandleDistance = std::abs(mouseX - selectionMinX);
  const float endHandleDistance = std::abs(mouseX - selectionMaxX);
  const bool isHandleHovered =
      std::min(startHandleDistance, endHandleDistance) <= handleHitRadius;
  const bool isSelectionHovered =
      mouseX > selectionMinX && mouseX < selectionMaxX;
  if (isHovered || isActive) {
    ImGui::SetMouseCursor(isHandleHovered      ? ImGuiMouseCursor_ResizeEW
                          : isSelectionHovered ? ImGuiMouseCursor_ResizeAll
                                               : ImGuiMouseCursor_Hand);
  }

  if (ImGui::IsItemActivated()) {
    if (isHandleHovered) {
      timelineDragMode_ = startHandleDistance <= endHandleDistance
                              ? TimelineDragMode::Start
                              : TimelineDragMode::End;
      timelineDragTarget_ = TimelineDragTarget::PlotVisible;
      timelineDragInitialRange_ = visibleTimeRange_;
    } else if (isSelectionHovered) {
      timelineDragMode_ = TimelineDragMode::Window;
      timelineDragTarget_ = TimelineDragTarget::PlotVisible;
      timelineDragInitialRange_ = visibleTimeRange_;
    } else {
      timelineDragMode_ = TimelineDragMode::Window;
      timelineDragTarget_ = TimelineDragTarget::TimelineView;
      timelineDragInitialRange_ = timelineViewRange_;
    }
    timelineDragAnchorSec_ = xToTime(mouseX);
    timelineDragAxisRange_ = timelineViewRange_;
    SetLiveView(false);
  }

  if (isActive && timelineDragTarget_ != TimelineDragTarget::None) {
    const double mouseRatio = ClampToOrderedRange(
        static_cast<double>((ImGui::GetIO().MousePos.x - barMin.x) / barWidth),
        0.0,
        1.0);
    const double mouseTime =
        timelineDragAxisRange_.minSec
        + mouseRatio
              * (timelineDragAxisRange_.maxSec - timelineDragAxisRange_.minSec);
    if (timelineDragTarget_ == TimelineDragTarget::TimelineView) {
      const double duration =
          timelineDragInitialRange_.maxSec - timelineDragInitialRange_.minSec;
      const double desiredMin =
          timelineDragInitialRange_.minSec + mouseTime - timelineDragAnchorSec_;
      timelineViewRange_ = {desiredMin, desiredMin + duration};
      ClampTimelineViewRangeToHistory();
      timelineViewWindowSec_ =
          timelineViewRange_.maxSec - timelineViewRange_.minSec;
    } else if (timelineDragMode_ == TimelineDragMode::Start) {
      visibleTimeRange_.minSec = std::min(mouseTime,
          visibleTimeRange_.maxSec - MinimumTimelineWindowSec);
      ClampVisibleTimeRangeToHistory();
      EnsureVisibleTimeRangeInTimelineView();
      liveWindowSec_ = visibleTimeRange_.maxSec - visibleTimeRange_.minSec;
      timelineViewWindowSec_ =
          timelineViewRange_.maxSec - timelineViewRange_.minSec;
      UpdateSharedXAxisTicks();
    } else if (timelineDragMode_ == TimelineDragMode::End) {
      visibleTimeRange_.maxSec = std::max(mouseTime,
          visibleTimeRange_.minSec + MinimumTimelineWindowSec);
      ClampVisibleTimeRangeToHistory();
      EnsureVisibleTimeRangeInTimelineView();
      liveWindowSec_ = visibleTimeRange_.maxSec - visibleTimeRange_.minSec;
      timelineViewWindowSec_ =
          timelineViewRange_.maxSec - timelineViewRange_.minSec;
      UpdateSharedXAxisTicks();
    } else {
      const double duration =
          timelineDragInitialRange_.maxSec - timelineDragInitialRange_.minSec;
      const double desiredMin =
          timelineDragInitialRange_.minSec + mouseTime - timelineDragAnchorSec_;
      visibleTimeRange_ = {desiredMin, desiredMin + duration};
      ClampVisibleTimeRangeToHistory();
      EnsureVisibleTimeRangeInTimelineView();
      liveWindowSec_ = visibleTimeRange_.maxSec - visibleTimeRange_.minSec;
      timelineViewWindowSec_ =
          timelineViewRange_.maxSec - timelineViewRange_.minSec;
      UpdateSharedXAxisTicks();
    }
  }
  if (ImGui::IsItemDeactivated()) {
    timelineDragMode_ = TimelineDragMode::None;
    timelineDragTarget_ = TimelineDragTarget::None;
  }
}

void MonitorView::DrawLinearizationTrack(
    std::span<const gnc::DynamicModeSnapshot> dynamicModeHistory) {
  ImGui::TextDisabled("Linearization");

  const TimelineRange trackRange = timelineViewRange_;
  const double trackDuration = trackRange.maxSec - trackRange.minSec;
  if (!std::isfinite(trackDuration) || trackDuration <= 0.0) {
    return;
  }

  const float trackWidth = std::max(UI::Ui(80.0F),
      ImGui::GetContentRegionAvail().x
          - UI::Ui(TimelineHorizontalPadding) * 2.0F);
  const float trackHeight = UI::Ui(LinearizationTrackHeight);
  const ImVec2 cursorPosition = ImGui::GetCursorScreenPos();
  const ImVec2 trackMin(cursorPosition.x + UI::Ui(TimelineHorizontalPadding),
      cursorPosition.y);
  const ImVec2 trackMax(trackMin.x + trackWidth, trackMin.y + trackHeight);
  const auto timeToX = [&](double timeSec) {
    const double ratio =
        ClampToOrderedRange((timeSec - trackRange.minSec) / trackDuration,
            0.0,
            1.0);
    return trackMin.x + static_cast<float>(ratio) * trackWidth;
  };
  const auto xToTime = [&](float x) {
    const double ratio =
        ClampToOrderedRange(static_cast<double>((x - trackMin.x) / trackWidth),
            0.0,
            1.0);
    return trackRange.minSec + ratio * trackDuration;
  };

  ImDrawList *drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(trackMin,
      trackMax,
      ImGui::GetColorU32(ImGuiCol_FrameBg),
      UI::Ui(3.0F));

  if (!dynamicModeHistory.empty()) {
    const ImU32 markerColor = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
    for (const gnc::DynamicModeSnapshot &snapshot : dynamicModeHistory) {
      if (snapshot.simulationTimeSec < trackRange.minSec
          || snapshot.simulationTimeSec > trackRange.maxSec) {
        continue;
      }
      const float markerX = timeToX(snapshot.simulationTimeSec);
      const ImVec2 markerCenter(markerX, (trackMin.y + trackMax.y) * 0.5F);
      drawList->AddLine(ImVec2(markerX, trackMin.y + UI::Ui(2.0F)),
          ImVec2(markerX, trackMax.y - UI::Ui(2.0F)),
          markerColor,
          UI::Ui(1.0F));
      drawList->AddCircleFilled(markerCenter,
          UI::Ui(LinearizationMarkerRadius),
          markerColor);
    }
  }

  if (selectedTimeInitialized_ && selectedTimeSec_ >= trackRange.minSec
      && selectedTimeSec_ <= trackRange.maxSec) {
    const float selectedX = timeToX(selectedTimeSec_);
    drawList->AddLine(ImVec2(selectedX, trackMin.y - UI::Ui(2.0F)),
        ImVec2(selectedX, trackMax.y + UI::Ui(2.0F)),
        ImGui::GetColorU32(ImVec4(0.95F, 0.75F, 0.25F, 0.9F)),
        UI::Ui(1.5F));
  }

  ImGui::SetCursorScreenPos(trackMin);
  ImGui::InvisibleButton("##LinearizationTrackInteraction",
      ImVec2(trackWidth, trackHeight),
      ImGuiButtonFlags_MouseButtonLeft);
  const bool isHovered = ImGui::IsItemHovered();
  const bool isActive = ImGui::IsItemActive();

  const gnc::DynamicModeSnapshot *hoveredSnapshot = nullptr;
  float nearestDistance = UI::Ui(LinearizationMarkerHitRadius) + 1.0F;
  if ((isHovered || isActive) && !dynamicModeHistory.empty()) {
    const float mouseX = ImGui::GetIO().MousePos.x;
    for (const gnc::DynamicModeSnapshot &snapshot : dynamicModeHistory) {
      if (snapshot.simulationTimeSec < trackRange.minSec
          || snapshot.simulationTimeSec > trackRange.maxSec) {
        continue;
      }
      const float distance =
          std::abs(mouseX - timeToX(snapshot.simulationTimeSec));
      if (distance < nearestDistance) {
        nearestDistance = distance;
        hoveredSnapshot = &snapshot;
      }
    }
  }

  if (isHovered) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (hoveredSnapshot != nullptr) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted("Linearization");
      ImGui::Text("Time: %.3f s", hoveredSnapshot->simulationTimeSec);
      ImGui::EndTooltip();
    }
  }

  if (ImGui::IsItemActivated()) {
    if (hoveredSnapshot != nullptr) {
      linearizationTrackSnapTimeSec_ = hoveredSnapshot->simulationTimeSec;
      SelectTimelineTime(*linearizationTrackSnapTimeSec_, true);
    } else {
      linearizationTrackSnapTimeSec_.reset();
      SelectTimelineTime(xToTime(ImGui::GetIO().MousePos.x), true);
    }
  } else if (isActive) {
    const float dragDistance =
        std::abs(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x);
    if (dragDistance > ImGui::GetIO().MouseDragThreshold) {
      linearizationTrackSnapTimeSec_.reset();
    }
    if (!linearizationTrackSnapTimeSec_) {
      SelectTimelineTime(xToTime(ImGui::GetIO().MousePos.x), true);
    }
  }
  if (ImGui::IsItemDeactivated()) {
    linearizationTrackSnapTimeSec_.reset();
  }
}

void MonitorView::DrawPlotList(const TelemetrySources &sources) {
  DrawPlotTable(sources, 1, PlotHeight, "MonitorPlotList");
}

void MonitorView::DrawPlotGrid(const TelemetrySources &sources, int dimension) {
  const char *tableId =
      dimension == 2 ? "MonitorPlotGrid2x2" : "MonitorPlotGrid3x3";
  DrawPlotTable(sources,
      dimension,
      CalculateGridPlotHeight(dimension),
      tableId);
}

void MonitorView::DrawPlotTable(const TelemetrySources &sources,
    int columnCount, float plotHeight, const char *tableId) {
  const std::size_t visiblePlotCount =
      static_cast<std::size_t>(std::count_if(plots_.begin(),
          plots_.end(),
          [this](const MonitorPlot &plot) { return IsPlotVisible(plot); }));
  if (visiblePlotCount == 0) {
    ImGui::TextDisabled(plots_.empty()
                            ? "No plots. Use + Plot or add a telemetry channel."
                            : "No visible plots. Enable one in the Explorer.");
    return;
  }

  constexpr ImGuiTableFlags Flags =
      ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings;
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
      ImVec2(UI::Ui(PlotGridCellPadding), 0.0F));
  if (!ImGui::BeginTable(tableId, columnCount, Flags)) {
    ImGui::PopStyleVar();
    return;
  }
  for (int column = 0; column < columnCount; ++column) {
    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);
  }

  std::optional<std::uint64_t> plotToDelete;
  std::size_t visibleIndex = 0;
  for (MonitorPlot &plot : plots_) {
    if (!IsPlotVisible(plot)) {
      continue;
    }

    if (visibleIndex % static_cast<std::size_t>(columnCount) == 0) {
      ImGui::TableNextRow();
    }
    ImGui::TableNextColumn();
    if (DrawPlotCard(plot, sources, plotHeight)) {
      plotToDelete = plot.id;
    }
    ++visibleIndex;
  }
  ImGui::EndTable();
  ImGui::PopStyleVar();

  if (plotToDelete) {
    DeletePlot(*plotToDelete);
  }
}

float MonitorView::CalculateGridPlotHeight(int rowCount) const {
  const float availableHeight = ImGui::GetContentRegionAvail().y;
  const float cardChromeHeight =
      UI::Ui(PlotCardTopMargin) + ImGui::GetTextLineHeight()
      + UI::Ui(PlotTitleFrameSpacing) + UI::Ui(PlotCardBottomMargin);
  const float plotHeightPixels =
      availableHeight / static_cast<float>(rowCount) - cardChromeHeight;
  const float uiScale = std::max(UI::GetUIScale(), 0.001F);
  return std::max(MinimumGridPlotHeight, plotHeightPixels / uiScale);
}

bool MonitorView::DrawPlotCard(MonitorPlot &plot,
    const TelemetrySources &sources, float plotHeight) {
  ImGui::PushID(static_cast<int>(plot.id));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
      ImVec2(UI::Ui(WorkspaceSpacing), 0.0F));
  ImGui::BeginGroup();

  ImGui::Dummy(ImVec2(0.0F, UI::Ui(PlotCardTopMargin)));

  const bool isSelected = selectedPlotId_ == plot.id;
  if (isSelected) {
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_TextLink));
  }
  ImGui::TextUnformatted(plot.title.c_str());
  if (isSelected) {
    ImGui::PopStyleColor();
  }
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    selectedPlotId_ = plot.id;
  }

  bool deleteRequested = false;
  if (ImGui::BeginPopupContextItem("PlotContextMenu")) {
    char titleBuffer[128]{};
    std::snprintf(titleBuffer, sizeof(titleBuffer), "%s", plot.title.c_str());
    ImGui::SetNextItemWidth(UI::Ui(220.0F));
    if (ImGui::InputText("Title", titleBuffer, sizeof(titleBuffer))) {
      plot.title = titleBuffer;
    }
    if (ImGui::MenuItem("Delete Plot")) {
      deleteRequested = true;
    }
    ImGui::EndPopup();
  }

  if (!deleteRequested) {
    ImGui::Dummy(ImVec2(0.0F, UI::Ui(PlotTitleFrameSpacing)));
    DrawTelemetryPlot(plot, sources, plotHeight);
    ImGui::Dummy(ImVec2(0.0F, UI::Ui(PlotCardBottomMargin)));
  }

  ImGui::EndGroup();
  ImGui::PopStyleVar();
  ImGui::PopID();
  return deleteRequested;
}

void MonitorView::DrawTelemetryPlot(const MonitorPlot &plot,
    const TelemetrySources &sources, float plotHeight) {
  const telemetry::TelemetrySnapshot &telemetry = *sources.primary;
  const float availablePlotWidth = ImGui::GetContentRegionAvail().x;
  const std::size_t maximumRenderedSampleCount = std::clamp(
      static_cast<std::size_t>(std::max(0.0F, availablePlotWidth) * 2.0F),
      MinimumRenderedSamplesPerChannel,
      MaximumRenderedSamplesPerChannel);
  UI::PlotBuilder plotBuilder =
      UI::Plot("##TelemetryPlot" + std::to_string(plot.id))
          .Height(plotHeight)
          .Flags(ImPlotFlags_NoTitle | ImPlotFlags_NoInputs
                 | ImPlotFlags_NoBoxSelect)
          .FocusedYAxis()
          .XAxisLinks(visibleTimeRange_.minSec, visibleTimeRange_.maxSec)
          .XAxisTicks(sharedXAxisTicks_)
          .XAxisLabel("Time (s)")
          .YAxisLabel(plot.yAxisLabel);

  plotBuilder.Underlay([this, &plot, &telemetry, maximumRenderedSampleCount] {
    DrawRollTrackingAcceptanceUnderlay(plot,
        telemetry,
        maximumRenderedSampleCount);
  });

  std::size_t renderedChannelCount = 0;
  std::vector<std::vector<telemetry::TelemetrySample>> renderedSeries;
  renderedSeries.reserve(plot.channels.size() * 2);
  const auto addSource = [this,
                             &plot,
                             &plotBuilder,
                             &renderedChannelCount,
                             &renderedSeries,
                             maximumRenderedSampleCount](
                             const telemetry::TelemetrySnapshot *snapshot,
                             std::string_view sourceName) {
    if (snapshot == nullptr) {
      return;
    }

    for (const std::string &path : plot.channels) {
      const telemetry::TelemetrySeries *channel = snapshot->Find(path);
      if (channel == nullptr || channel->samples.empty()) {
        continue;
      }

      renderedSeries.push_back(telemetry::ReadTelemetrySamples(*channel,
          visibleTimeRange_.minSec,
          visibleTimeRange_.maxSec,
          maximumRenderedSampleCount));
      const std::vector<telemetry::TelemetrySample> &samples =
          renderedSeries.back();
      if (samples.empty()) {
        renderedSeries.pop_back();
        continue;
      }
      const telemetry::TelemetrySample *data = samples.data();
      plotBuilder.AddLine(MakeTelemetrySeriesLabel(path, sourceName),
          UI::DataView(&data->timeSec,
              samples.size(),
              sizeof(telemetry::TelemetrySample)),
          UI::DataView(&data->value,
              samples.size(),
              sizeof(telemetry::TelemetrySample)));
      ++renderedChannelCount;
    }
  };
  addSource(sources.primary.get(), "Primary");
  addSource(sources.baseline.get(), "Baseline");

  plotBuilder.Overlay(
      [this, &plot, &telemetry] { DrawPlotOverlay(plot, telemetry); });
  UI::UIElement plotElement = plotBuilder;
  plotElement.Render();

  if (plot.channels.empty()) {
    ImGui::TextDisabled("No channels assigned.");
  } else if (renderedChannelCount == 0) {
    ImGui::TextDisabled("Waiting for assigned telemetry channels.");
  }
}

void MonitorView::DrawRollTrackingAcceptanceUnderlay(const MonitorPlot &plot,
    const telemetry::TelemetrySnapshot &telemetry,
    std::size_t maximumRenderedSampleCount) const {
  const std::string_view rollTrackingPath =
      GetTelemetryPlotBinding(DefaultTelemetryPlot::RollHoldRollTracking)
          .nodePath;
  if (plot.telemetryGroupPath != rollTrackingPath) {
    return;
  }

  const telemetry::TelemetrySeries *commandChannel =
      telemetry.Find(telemetry::paths::AutopilotRollHoldCommandedRoll);
  if (commandChannel == nullptr) {
    return;
  }

  const std::vector<telemetry::TelemetrySample> samples =
      telemetry::ReadTelemetrySamples(*commandChannel,
          visibleTimeRange_.minSec,
          visibleTimeRange_.maxSec,
          maximumRenderedSampleCount);
  if (samples.size() < 2) {
    return;
  }

  const ImU32 limitBoundaryColor =
      ImGui::ColorConvertFloat4ToU32(ImVec4(0.90F, 0.68F, 0.34F, 0.22F));
  ImPlot::PushPlotClipRect();
  DrawDashedPlotLine(samples,
      RollLimitToleranceDeg,
      limitBoundaryColor,
      UI::Ui(1.0F),
      UI::Ui(4.0F),
      std::max(1.0F, UI::Ui(0.7F)));
  DrawDashedPlotLine(samples,
      -RollLimitToleranceDeg,
      limitBoundaryColor,
      UI::Ui(1.0F),
      UI::Ui(4.0F),
      std::max(1.0F, UI::Ui(0.7F)));
  ImPlot::PopPlotClipRect();
}

bool MonitorView::IsPlotVisible(const MonitorPlot &plot) const {
  return plot.manualVisible || IsPlotVisibleByPreset(plot);
}

bool MonitorView::IsPlotVisibleByPreset(const MonitorPlot &plot) const {
  for (std::size_t presetIndex = 0;
      presetIndex < MonitorPresetDefinitions.size();
      ++presetIndex) {
    if (!IsPresetActive(presetIndex)) {
      continue;
    }

    const MonitorPresetDefinition &preset =
        MonitorPresetDefinitions[presetIndex];
    for (std::size_t plotIndex = 0; plotIndex < preset.requiredPlotCount;
        ++plotIndex) {
      const TelemetryPlotBinding &binding =
          GetTelemetryPlotBinding(preset.requiredPlots[plotIndex]);
      if (plot.telemetryGroupPath == binding.nodePath) {
        return true;
      }
    }
  }
  return false;
}

bool MonitorView::IsPresetActive(std::size_t presetIndex) const {
  return (activePresetMask_
             & GetPresetBit(MonitorPresetDefinitions[presetIndex].preset))
         != 0;
}

void MonitorView::SetPresetActive(std::size_t presetIndex, bool active) {
  const std::uint32_t presetBit =
      GetPresetBit(MonitorPresetDefinitions[presetIndex].preset);
  if (active) {
    activePresetMask_ |= presetBit;
  } else {
    activePresetMask_ &= ~presetBit;
  }
}

std::optional<MonitorView::TimelineRange> MonitorView::GetTelemetryHistoryRange(
    const telemetry::TelemetrySnapshot &telemetry) const {
  const std::optional<telemetry::TelemetryTimeRange> range =
      telemetry.publishedTimeRange;
  if (!range) {
    return std::nullopt;
  }

  return TimelineRange{std::min(0.0, range->minSec), range->maxSec};
}

void MonitorView::SynchronizeTimelineState(
    const telemetry::TelemetrySnapshot &telemetry) {
  telemetryHistoryRange_ = GetTelemetryHistoryRange(telemetry);
  if (!telemetryHistoryRange_) {
    selectedTimeInitialized_ = false;
    return;
  }

  if (liveView_) {
    UpdateLiveTimeRanges();
    selectedTimeSec_ = telemetryHistoryRange_->maxSec;
    selectedTimeInitialized_ = true;
  } else {
    ClampTimelineViewRangeToHistory();
    ClampVisibleTimeRangeToHistory();
    if (!selectedTimeInitialized_) {
      selectedTimeSec_ = visibleTimeRange_.maxSec;
      selectedTimeInitialized_ = true;
    } else {
      selectedTimeSec_ = ClampToOrderedRange(selectedTimeSec_,
          telemetryHistoryRange_->minSec,
          telemetryHistoryRange_->maxSec);
    }
  }
  UpdateSharedXAxisTicks();
}

MonitorView::TimelineRange MonitorView::GetEffectiveHistoryRange(
    const TimelineRange &historyRange) const {
  if (historyRange.maxSec - historyRange.minSec >= MinimumTimelineWindowSec) {
    return historyRange;
  }
  return {historyRange.minSec, historyRange.minSec + MinimumTimelineWindowSec};
}

void MonitorView::ClampTimelineViewRangeToHistory() {
  if (!telemetryHistoryRange_) {
    return;
  }

  const TimelineRange historyRange =
      GetEffectiveHistoryRange(*telemetryHistoryRange_);
  const double historyDuration = historyRange.maxSec - historyRange.minSec;
  double duration = timelineViewRange_.maxSec - timelineViewRange_.minSec;
  const bool isFiniteRange = std::isfinite(timelineViewRange_.minSec)
                             && std::isfinite(timelineViewRange_.maxSec);
  const bool isInsideHistory =
      isFiniteRange && duration >= MinimumTimelineWindowSec
      && duration <= historyDuration
      && timelineViewRange_.minSec >= historyRange.minSec
      && timelineViewRange_.maxSec <= historyRange.maxSec;
  if (isInsideHistory) {
    return;
  }

  if (!std::isfinite(duration) || duration < MinimumTimelineWindowSec) {
    duration = MinimumTimelineWindowSec;
  }
  duration = std::min(duration, historyDuration);
  double minSec = timelineViewRange_.minSec;
  if (!std::isfinite(minSec)) {
    minSec = historyRange.minSec;
  }
  const double maximumMinSec = historyRange.maxSec - duration;
  minSec = ClampToOrderedRange(minSec, historyRange.minSec, maximumMinSec);
  timelineViewRange_ = {minSec, minSec + duration};
}

void MonitorView::ClampVisibleTimeRangeToHistory() {
  if (!telemetryHistoryRange_) {
    return;
  }

  const TimelineRange historyRange =
      GetEffectiveHistoryRange(*telemetryHistoryRange_);
  const double historyDuration = std::max(MinimumTimelineWindowSec,
      historyRange.maxSec - historyRange.minSec);
  double duration = visibleTimeRange_.maxSec - visibleTimeRange_.minSec;
  const bool isFiniteRange = std::isfinite(visibleTimeRange_.minSec)
                             && std::isfinite(visibleTimeRange_.maxSec);
  const bool hasValidDuration = isFiniteRange
                                && duration >= MinimumTimelineWindowSec
                                && duration <= historyDuration;
  const bool isInsideHistory =
      hasValidDuration && visibleTimeRange_.minSec >= historyRange.minSec
      && visibleTimeRange_.maxSec <= historyRange.maxSec;
  if (isInsideHistory) {
    return;
  }

  if (!std::isfinite(duration) || duration < MinimumTimelineWindowSec) {
    duration = MinimumTimelineWindowSec;
  }
  duration = std::min(duration, historyDuration);

  double minSec = visibleTimeRange_.minSec;
  if (!std::isfinite(minSec)) {
    minSec = historyRange.minSec;
  }
  const double maximumMinSec =
      std::max(historyRange.minSec, historyRange.maxSec - duration);
  minSec = ClampToOrderedRange(minSec, historyRange.minSec, maximumMinSec);
  visibleTimeRange_ = {minSec, minSec + duration};
}

void MonitorView::EnsureVisibleTimeRangeInTimelineView() {
  const double visibleDuration =
      visibleTimeRange_.maxSec - visibleTimeRange_.minSec;
  double viewDuration = timelineViewRange_.maxSec - timelineViewRange_.minSec;
  if (visibleDuration > viewDuration) {
    timelineViewRange_ = visibleTimeRange_;
    viewDuration = visibleDuration;
  } else if (visibleTimeRange_.minSec < timelineViewRange_.minSec) {
    timelineViewRange_.minSec = visibleTimeRange_.minSec;
    timelineViewRange_.maxSec = timelineViewRange_.minSec + viewDuration;
  } else if (visibleTimeRange_.maxSec > timelineViewRange_.maxSec) {
    timelineViewRange_.maxSec = visibleTimeRange_.maxSec;
    timelineViewRange_.minSec = timelineViewRange_.maxSec - viewDuration;
  }
  ClampTimelineViewRangeToHistory();
}

void MonitorView::UpdateSharedXAxisTicks() {
  sharedXAxisTicks_ = CalculateTimelineTicks(visibleTimeRange_.minSec,
      visibleTimeRange_.maxSec);
}

void MonitorView::UpdateLiveTimeRanges() {
  if (!telemetryHistoryRange_) {
    timelineViewRange_ = {0.0, timelineViewWindowSec_};
    visibleTimeRange_ = {0.0, liveWindowSec_};
    return;
  }

  const TimelineRange historyRange =
      GetEffectiveHistoryRange(*telemetryHistoryRange_);
  const double historyDuration = std::max(MinimumTimelineWindowSec,
      historyRange.maxSec - historyRange.minSec);
  const double visibleDuration = std::min(liveWindowSec_, historyDuration);
  const double viewDuration =
      std::min(std::max(timelineViewWindowSec_, visibleDuration),
          historyDuration);
  timelineViewRange_.maxSec = historyRange.maxSec;
  timelineViewRange_.minSec = historyRange.maxSec - viewDuration;
  visibleTimeRange_.maxSec = historyRange.maxSec;
  visibleTimeRange_.minSec = historyRange.maxSec - visibleDuration;
}

void MonitorView::SetLiveView(bool enabled) {
  if (liveView_ == enabled) {
    return;
  }

  liveView_ = enabled;
  events_.Emit(MonitorLiveChanged{enabled});
  if (liveView_) {
    UpdateLiveTimeRanges();
    if (telemetryHistoryRange_) {
      selectedTimeSec_ = telemetryHistoryRange_->maxSec;
      selectedTimeInitialized_ = true;
    }
    UpdateSharedXAxisTicks();
  }
}

void MonitorView::SelectTimelineTime(double timeSec, bool disableLive) {
  if (!telemetryHistoryRange_ || !std::isfinite(timeSec)) {
    return;
  }

  if (disableLive) {
    SetLiveView(false);
  }
  selectedTimeSec_ = ClampToOrderedRange(timeSec,
      telemetryHistoryRange_->minSec,
      telemetryHistoryRange_->maxSec);
  selectedTimeInitialized_ = true;
  events_.Emit(MonitorCursorMoved{selectedTimeSec_});
}

void MonitorView::ZoomTimelineView(double wheelDelta, double anchorSec) {
  if (!telemetryHistoryRange_ || !std::isfinite(wheelDelta)
      || wheelDelta == 0.0) {
    return;
  }
  events_.Emit(MonitorZoomRequested{wheelDelta, anchorSec});

  const TimelineRange historyRange =
      GetEffectiveHistoryRange(*telemetryHistoryRange_);
  const double historyDuration = std::max(MinimumTimelineWindowSec,
      historyRange.maxSec - historyRange.minSec);
  const double visibleDuration =
      visibleTimeRange_.maxSec - visibleTimeRange_.minSec;
  const double minimumViewDuration = std::min(historyDuration,
      std::max(MinimumTimelineWindowSec, visibleDuration));
  const double currentDuration =
      ClampToOrderedRange(timelineViewRange_.maxSec - timelineViewRange_.minSec,
          MinimumTimelineWindowSec,
          historyDuration);
  const double zoomMultiplier = std::pow(TimelineZoomFactor, -wheelDelta);
  const double newDuration =
      ClampToOrderedRange(currentDuration * zoomMultiplier,
          minimumViewDuration,
          historyDuration);

  timelineViewWindowSec_ = newDuration;
  if (liveView_) {
    timelineViewRange_.maxSec = historyRange.maxSec;
    timelineViewRange_.minSec = historyRange.maxSec - newDuration;
  } else {
    const double anchorRatio =
        (anchorSec - timelineViewRange_.minSec) / currentDuration;
    timelineViewRange_.minSec = anchorSec - newDuration * anchorRatio;
    timelineViewRange_.maxSec = timelineViewRange_.minSec + newDuration;
    ClampTimelineViewRangeToHistory();
  }
}

void MonitorView::DrawPlotOverlay(const MonitorPlot &plot,
    const telemetry::TelemetrySnapshot &telemetry) {
  const bool isHovered = ImPlot::IsPlotHovered();
  if (isHovered) {
    const ImPlotRect limits = ImPlot::GetPlotLimits();
    SelectTimelineTime(ClampToOrderedRange(ImPlot::GetPlotMousePos().x,
                           limits.X.Min,
                           limits.X.Max),
        false);
  }

  if (selectedTimeInitialized_) {
    ImPlotSpec cursorSpec;
    cursorSpec.LineColor = ImVec4(0.95F, 0.75F, 0.25F, 0.9F);
    cursorSpec.Flags = ImPlotItemFlags_NoLegend;
    ImPlot::PlotInfLines("##SharedTimeCursor",
        &selectedTimeSec_,
        1,
        cursorSpec);
  }

  if (!isHovered) {
    return;
  }

  ImGui::BeginTooltip();
  ImGui::Text("t = %.3f s", selectedTimeSec_);
  ImGui::Separator();
  for (const std::string &path : plot.channels) {
    const telemetry::TelemetrySeries *channel = telemetry.Find(path);
    if (channel == nullptr) {
      continue;
    }
    const std::optional<telemetry::TelemetrySample> sample =
        telemetry::FindClosestTelemetrySample(*channel, selectedTimeSec_);
    if (sample) {
      ImGui::Text("%s: %.6g", path.c_str(), sample->value);
    }
  }
  ImGui::EndTooltip();
}
} // namespace gui
