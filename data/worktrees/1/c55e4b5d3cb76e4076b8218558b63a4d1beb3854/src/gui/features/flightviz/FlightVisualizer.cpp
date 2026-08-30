#include "gui/features/flightviz/FlightVisualizer.hpp"

#include "gui/resources/EditorIcon.hpp"
#include "flightui/visualization/components/AltitudeCueRenderer.hpp"
#include "flightui/visualization/components/AircraftWireframeRenderer.hpp"
#include "flightui/visualization/components/FlightCameraController.hpp"
#include "flightui/visualization/components/GroundGridRenderer.hpp"
#include "flightui/visualization/components/TelemetryOverlay.hpp"
#include "flightui/visualization/render/CameraComponent.hpp"
#include "flightui/visualization/render/LineCanvas.hpp"
#include "sim/runtime/SimulationContracts.hpp"
#include "common/math/Math.hpp"
#include "flightui/FlightUI.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
constexpr float MinVisualAltitude = 0.35F;
constexpr float MaxVisualAltitude = 52.0F;
constexpr float LinearAltitudeBreakFt = 1800.0F;
constexpr float FeetPerVizUnit = 75.0F;
constexpr float HighAltitudeLogFt = 450.0F;
constexpr float HighAltitudeLogScale = 8.0F;
constexpr float MetersPerVizUnit = FeetPerVizUnit * 0.3048F;
constexpr float AircraftOriginZ = 0.35F;
constexpr double EarthRadiusMeters = 6'371'000.0;
constexpr double MaxMotionTickSec = 0.25;
constexpr double KnotsToMetersPerSec = 0.5144444444444445;
constexpr double MinimumMinimapSpanMeters = 100.0;
constexpr float MinimapHorizontalDirection = -1.0F;
constexpr float ToolbarHeight = 28.0F;
constexpr float ToolbarButtonSize = 22.0F;
constexpr float ToolbarViewportSpacing = 1.0F;

ImVec2 Offset(ImVec2 point, float x, float y) {
  return {point.x + x, point.y + y};
}

float VisualAltitudeFromAglFt(double altitudeAglFt) {
  if (!std::isfinite(altitudeAglFt)) {
    return MinVisualAltitude;
  }

  const float altitudeFt = static_cast<float>(std::max(altitudeAglFt, 0.0));
  if (altitudeFt <= LinearAltitudeBreakFt) {
    return std::clamp(altitudeFt / FeetPerVizUnit,
        MinVisualAltitude,
        MaxVisualAltitude);
  }

  const float linearAltitude = LinearAltitudeBreakFt / FeetPerVizUnit;
  const float compressedAltitude =
      linearAltitude
      + std::log1p((altitudeFt - LinearAltitudeBreakFt) / HighAltitudeLogFt)
            * HighAltitudeLogScale;
  return std::clamp(compressedAltitude, MinVisualAltitude, MaxVisualAltitude);
}

float HorizontalSpeedMps(const sim::AircraftState &state) {
  if (std::isfinite(state.trueAirspeedMps) && state.trueAirspeedMps > 0.1) {
    return static_cast<float>(state.trueAirspeedMps);
  }

  if (std::isfinite(state.calibratedAirspeedKts)
      && state.calibratedAirspeedKts > 0.1) {
    return static_cast<float>(
        state.calibratedAirspeedKts * KnotsToMetersPerSec);
  }

  return 0.0F;
}
} // namespace

namespace viz {
FlightVisualizer::FlightVisualizer() { BuildScene(); }

FlightVisualizer::~FlightVisualizer() = default;

void FlightVisualizer::SetShadowEnabled(bool enabled) {
  shadowEnabled_ = enabled;
  UpdateSnapshotViewState();
}

bool FlightVisualizer::Tick(const sim::SimulationInstanceSnapshot *mainSnapshot,
    const sim::SimulationInstanceSnapshot *shadowSnapshot) {
  if (mainSnapshot == nullptr || !mainSnapshot->available) {
    snapshot_.aircraft.available = false;
    snapshot_.shadowAircraft.available = false;
    return false;
  }

  const double simulationTimeSec = mainSnapshot->aircraft.simulationTimeSec;
  if (lastMainSimulationTimeSec_.has_value() && std::isfinite(simulationTimeSec)
      && simulationTimeSec < *lastMainSimulationTimeSec_) {
    ResetMainState();
  }
  if (std::isfinite(simulationTimeSec)) {
    lastMainSimulationTimeSec_ = simulationTimeSec;
  }

  UpdateWorldOrigin(*mainSnapshot);
  snapshot_.aircraft = CaptureAircraft(*mainSnapshot);
  snapshot_.shadowAircraft.available = false;
  if (shadowSnapshot != nullptr && shadowSnapshot->available) {
    snapshot_.shadowAircraft = CaptureAircraft(*shadowSnapshot);
  }

  SyncFlightPath(*mainSnapshot);
  SyncGroundScroll(snapshot_.aircraft.state);
  UpdateSnapshotViewState();
  scene_.Tick(snapshot_);
  return true;
}

void FlightVisualizer::SetViewMode(ViewMode mode) {
  viewMode_ = mode;
  UpdateSnapshotViewState();
}

void FlightVisualizer::RenderScene(const gui::EditorIconHandle &shadowIcon,
    const gui::EditorIconHandle &viewOptionsIcon,
    const gui::EditorIconHandle &cameraViewIcon, const char *shadowTooltip,
    const char *unavailableMessage,
    gui::architecture::EventSink<gui::FlightVizShadowVisibilityChanged>
        shadowEvents,
    gui::architecture::EventSink<gui::FlightVizCameraViewToggleRequested>
        cameraEvents,
    gui::architecture::EventSink<gui::FlightVizDisplayOptionsChanged>
        displayEvents,
    gui::architecture::EventSink<gui::FlightVizClearPathRequested> pathEvents) {
  RenderToolbar(shadowIcon,
      viewOptionsIcon,
      cameraViewIcon,
      shadowTooltip,
      shadowEvents,
      cameraEvents,
      displayEvents,
      pathEvents);

  if (!snapshot_.aircraft.available) {
    ImGui::TextDisabled("%s", unavailableMessage);
    return;
  }

  HandleInput(cameraEvents);
  UpdateSnapshotViewState();
  scene_.Tick(snapshot_);

  const ImVec2 available = ImGui::GetContentRegionAvail();
  const ImVec2 size{
      std::max(available.x, 1.0F),
      std::max(available.y, 1.0F),
  };

  ImGui::SetNextItemAllowOverlap();
  ImGui::InvisibleButton("##FlightVizCanvas", size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  const float focalLength = std::min(size.x, size.y) * 0.82F;

  ImDrawList *drawList = ImGui::GetWindowDrawList();
  drawList->PushClipRect(min, max, true);

  const CameraView camera =
      mainCamera_ != nullptr ? mainCamera_->BuildView() : CameraView{};
  LineCanvas canvas(*drawList, min, max, camera, focalLength);
  canvas.Fill(IM_COL32(13, 16, 21, 255));

  RenderContext context{snapshot_, canvas};
  scene_.Render(context);
  RenderMinimap(min, max);

  canvas.Border(IM_COL32(88, 96, 108, 255));
  drawList->PopClipRect();
  snapshot_.viewOptions = viewOptions_;
}

void FlightVisualizer::HandleInput(
    gui::architecture::EventSink<gui::FlightVizCameraViewToggleRequested>
        cameraEvents) {
  const ImGuiIO &io = ImGui::GetIO();
  if (io.WantTextInput
      || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    return;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
    cameraEvents.Emit({});
  }
}

void FlightVisualizer::RenderToolbar(const gui::EditorIconHandle &shadowIcon,
    const gui::EditorIconHandle &viewOptionsIcon,
    const gui::EditorIconHandle &cameraViewIcon, const char *shadowTooltip,
    gui::architecture::EventSink<gui::FlightVizShadowVisibilityChanged>
        shadowEvents,
    gui::architecture::EventSink<gui::FlightVizCameraViewToggleRequested>
        cameraEvents,
    gui::architecture::EventSink<gui::FlightVizDisplayOptionsChanged>
        displayEvents,
    gui::architecture::EventSink<gui::FlightVizClearPathRequested> pathEvents) {
  const bool shadowAvailable = snapshot_.shadowAircraft.available;
  const bool thirdPerson = viewMode_ == ViewMode::ThirdPerson;
  const std::string shadowButtonTooltip =
      shadowAvailable ? shadowTooltip
                      : std::string(shadowTooltip) + "\nSimulation unavailable";
  FlightUI::Toolbar()
      .Id("FlightVizToolbar")
      .AlignRight()
      .Compact()
      .Height(ToolbarHeight)
      .Spacing(
          4.0F)[+FlightUI::ToggleIconButton("ShadowAircraftButton",
                    shadowIcon.texture,
                    shadowEnabled_)
                    .FallbackText("S")
                    .Size(ToolbarButtonSize)
                    .Enabled(shadowAvailable)
                    .Tooltip(shadowButtonTooltip)
                    .OnChanged([shadowEvents](bool enabled) {
                      shadowEvents.Emit({enabled});
                    })
                + FlightUI::IconButton("ViewOptionsButton",
                    viewOptionsIcon.texture)
                    .FallbackText("E")
                    .Size(ToolbarButtonSize)
                    .Tooltip("View options")
                    .OnAction([] { ImGui::OpenPopup("ViewOptions"); })
                + FlightUI::ToggleIconButton("CameraViewButton",
                    cameraViewIcon.texture,
                    thirdPerson)
                    .FallbackText("V")
                    .Size(ToolbarButtonSize)
                    .Tooltip(thirdPerson ? "Switch to orbit view"
                                         : "Switch to third-person view")
                    .OnChanged([cameraEvents](bool) { cameraEvents.Emit({}); })]
      .Render();
  RenderViewOptionsPopup(displayEvents, pathEvents);
  FlightUI::Space(ToolbarViewportSpacing).Render();
}

void FlightVisualizer::RenderViewOptionsPopup(
    gui::architecture::EventSink<gui::FlightVizDisplayOptionsChanged>
        displayEvents,
    gui::architecture::EventSink<gui::FlightVizClearPathRequested> pathEvents) {
  if (ImGui::BeginPopup("ViewOptions")) {
    ViewOptions options = viewOptions_;
    ImGui::TextDisabled("View");
    ImGui::Separator();
    const bool optionsChanged =
        ImGui::Checkbox("Ground Grid", &options.showGroundGrid)
        | ImGui::Checkbox("Telemetry", &options.showTelemetry)
        | ImGui::Checkbox("Minimap", &options.showMinimap);
    if (optionsChanged) {
      displayEvents.Emit(
          {options.showGroundGrid, options.showTelemetry, options.showMinimap});
    }
    if (options.showMinimap && ImGui::Button("Clear Minimap Path")) {
      pathEvents.Emit({});
    }
    ImGui::Separator();
    ImGui::TextDisabled("V: view");
    ImGui::EndPopup();
  }
}

void FlightVisualizer::RenderMinimap(ImVec2 min, ImVec2 max) {
  if (!viewOptions_.showMinimap) {
    return;
  }

  const auto current = flightPath_.GetCurrentPoint();
  if (!snapshot_.aircraft.available || !current.has_value()) {
    return;
  }
  const FlightPathPoint currentPoint = *current;

  const auto toWorldPoint = [currentPoint](const FlightPathPoint &point) {
    return FlightPathPoint{
        .northMeters = point.northMeters - currentPoint.northMeters,
        .eastMeters = point.eastMeters - currentPoint.eastMeters,
    };
  };

  const float canvasWidth = std::max(max.x - min.x, 1.0F);
  const float canvasHeight = std::max(max.y - min.y, 1.0F);
  const float maximumSize =
      std::max(std::min(canvasWidth * 0.34F, canvasHeight * 0.38F), 1.0F);
  const float expandedSize = std::min(FlightUI::Ui(210.0F), maximumSize);
  const float outerPadding =
      std::min(FlightUI::Ui(10.0F), expandedSize * 0.08F);
  const bool wasMinimized = minimapMinimized_;
  const float minimapWidth =
      wasMinimized ? std::min(FlightUI::Ui(112.0F),
                         std::max(canvasWidth - outerPadding, 1.0F))
                   : expandedSize;
  const float minimapHeight =
      wasMinimized ? std::min(FlightUI::Ui(30.0F),
                         std::max(canvasHeight - outerPadding, 1.0F))
                   : expandedSize;
  const ImVec2 mapMin{
      max.x - outerPadding - minimapWidth,
      min.y + outerPadding,
  };
  const ImVec2 mapMax{
      max.x - outerPadding,
      mapMin.y + minimapHeight,
  };

  ImDrawList &drawList = *ImGui::GetWindowDrawList();
  drawList.AddRectFilled(mapMin,
      mapMax,
      IM_COL32(30, 30, 30, 224),
      FlightUI::Ui(3.0F));
  drawList.AddRect(mapMin,
      mapMax,
      IM_COL32(76, 82, 90, 255),
      FlightUI::Ui(3.0F),
      0,
      FlightUI::Ui(1.0F));

  const float headerHeight =
      std::min(FlightUI::Ui(24.0F), minimapHeight * 0.8F);
  const float contentPadding = std::min(FlightUI::Ui(10.0F),
      std::min(minimapWidth, minimapHeight) * 0.08F);

  double minimumNorth = 0.0;
  double maximumNorth = 0.0;
  double minimumEast = 0.0;
  double maximumEast = 0.0;
  bool hasBounds = false;
  const auto includePoint = [&](const FlightPathPoint &point) {
    if (!hasBounds) {
      minimumNorth = maximumNorth = point.northMeters;
      minimumEast = maximumEast = point.eastMeters;
      hasBounds = true;
      return;
    }
    minimumNorth = std::min(minimumNorth, point.northMeters);
    maximumNorth = std::max(maximumNorth, point.northMeters);
    minimumEast = std::min(minimumEast, point.eastMeters);
    maximumEast = std::max(maximumEast, point.eastMeters);
  };
  for (const FlightPathPoint &point : flightPath_.GetPoints()) {
    includePoint(toWorldPoint(point));
  }
  includePoint({});

  const double centerNorth = (minimumNorth + maximumNorth) * 0.5;
  const double centerEast = (minimumEast + maximumEast) * 0.5;
  const double spanMeters = std::max({maximumNorth - minimumNorth,
      maximumEast - minimumEast,
      MinimumMinimapSpanMeters});

  char title[64]{};
  if (wasMinimized) {
    std::snprintf(title, sizeof(title), "PATH");
  } else {
    std::snprintf(title, sizeof(title), "PATH  %.0f m", spanMeters);
  }
  drawList.AddText(
      ImVec2(mapMin.x + contentPadding, mapMin.y + FlightUI::Ui(5.0F)),
      IM_COL32(214, 214, 214, 255),
      title);

  const float sizeButtonExtent =
      std::max(std::min(FlightUI::Ui(18.0F), headerHeight - FlightUI::Ui(4.0F)),
          1.0F);
  const ImVec2 sizeButtonPosition{
      mapMax.x - contentPadding - sizeButtonExtent,
      mapMin.y + (headerHeight - sizeButtonExtent) * 0.5F,
  };
  ImGui::PushID("FlightPathMinimap");
  ImGui::SetCursorScreenPos(sizeButtonPosition);
  if (ImGui::Button(wasMinimized ? "+" : "-",
          ImVec2(sizeButtonExtent, sizeButtonExtent))) {
    minimapMinimized_ = !wasMinimized;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(wasMinimized ? "Maximize minimap" : "Minimize minimap");
  }
  ImGui::PopID();

  if (wasMinimized) {
    return;
  }

  const ImVec2 plotMin{
      mapMin.x + contentPadding,
      mapMin.y + headerHeight,
  };
  const ImVec2 plotMax{
      mapMax.x - contentPadding,
      mapMax.y - contentPadding,
  };
  const float plotWidth = std::max(plotMax.x - plotMin.x, 1.0F);
  const float plotHeight = std::max(plotMax.y - plotMin.y, 1.0F);
  const float pixelsPerMeter =
      std::min(plotWidth, plotHeight) / static_cast<float>(spanMeters * 1.15);
  const ImVec2 plotCenter{
      (plotMin.x + plotMax.x) * 0.5F,
      (plotMin.y + plotMax.y) * 0.5F,
  };

  const auto projectPoint = [&](const FlightPathPoint &point) {
    return ImVec2(plotCenter.x
                      + MinimapHorizontalDirection
                            * static_cast<float>(point.eastMeters - centerEast)
                            * pixelsPerMeter,
        plotCenter.y
            - static_cast<float>(point.northMeters - centerNorth)
                  * pixelsPerMeter);
  };

  drawList.AddText(ImVec2(sizeButtonPosition.x - FlightUI::Ui(15.0F),
                       mapMin.y + FlightUI::Ui(5.0F)),
      IM_COL32(128, 156, 182, 255),
      "N");

  drawList.PushClipRect(plotMin, plotMax, true);
  drawList.AddLine(ImVec2(plotMin.x, plotCenter.y),
      ImVec2(plotMax.x, plotCenter.y),
      IM_COL32(63, 63, 63, 180),
      FlightUI::Ui(1.0F));
  drawList.AddLine(ImVec2(plotCenter.x, plotMin.y),
      ImVec2(plotCenter.x, plotMax.y),
      IM_COL32(63, 63, 63, 180),
      FlightUI::Ui(1.0F));

  const auto &points = flightPath_.GetPoints();
  if (!points.empty()) {
    auto pointIterator = points.begin();
    ImVec2 previousScreenPoint = projectPoint(toWorldPoint(*pointIterator));
    const ImVec2 startScreenPoint = previousScreenPoint;
    ++pointIterator;
    for (; pointIterator != points.end(); ++pointIterator) {
      const ImVec2 screenPoint = projectPoint(toWorldPoint(*pointIterator));
      drawList.AddLine(previousScreenPoint,
          screenPoint,
          IM_COL32(83, 151, 211, 230),
          FlightUI::Ui(2.0F));
      previousScreenPoint = screenPoint;
    }

    const ImVec2 currentScreenPoint = projectPoint({});
    drawList.AddLine(previousScreenPoint,
        currentScreenPoint,
        IM_COL32(83, 151, 211, 230),
        FlightUI::Ui(2.0F));
    drawList.AddCircleFilled(startScreenPoint,
        FlightUI::Ui(3.0F),
        IM_COL32(107, 166, 112, 230));

    const double courseRad = math::DegToRad(snapshot_.aircraft.state.courseDeg);
    const ImVec2 forward{
        MinimapHorizontalDirection * static_cast<float>(std::sin(courseRad)),
        static_cast<float>(-std::cos(courseRad)),
    };
    const ImVec2 right{-forward.y, forward.x};
    const float markerLength = FlightUI::Ui(9.0F);
    const float markerHalfWidth = FlightUI::Ui(5.0F);
    const ImVec2 markerTip{
        currentScreenPoint.x + forward.x * markerLength,
        currentScreenPoint.y + forward.y * markerLength,
    };
    const ImVec2 markerLeft{
        currentScreenPoint.x - forward.x * markerLength * 0.55F
            + right.x * markerHalfWidth,
        currentScreenPoint.y - forward.y * markerLength * 0.55F
            + right.y * markerHalfWidth,
    };
    const ImVec2 markerRight{
        currentScreenPoint.x - forward.x * markerLength * 0.55F
            - right.x * markerHalfWidth,
        currentScreenPoint.y - forward.y * markerLength * 0.55F
            - right.y * markerHalfWidth,
    };
    drawList.AddTriangleFilled(markerTip,
        markerLeft,
        markerRight,
        IM_COL32(230, 235, 240, 255));
  }
  drawList.PopClipRect();
}

AircraftSnapshot FlightVisualizer::CaptureAircraft(
    const sim::SimulationInstanceSnapshot &source) const {
  AircraftSnapshot snapshot;
  snapshot.state = source.aircraft;
  snapshot.controlInput = source.controlInput;
  snapshot.pitchTrim = source.pitchTrim;
  snapshot.position = ProjectWorldPosition(source);
  snapshot.visualAltitude =
      VisualAltitudeFromAglFt(snapshot.state.altitudeAglFt);
  snapshot.available = true;
  return snapshot;
}

void FlightVisualizer::ResetMainState() {
  snapshot_.aircraft = {};
  snapshot_.shadowAircraft = {};
  flightPath_.Reset();
  motion_ = {};
  worldOrigin_ = {};
  lastMainSimulationTimeSec_.reset();
  UpdateSnapshotViewState();
}

void FlightVisualizer::UpdateWorldOrigin(
    const sim::SimulationInstanceSnapshot &source) {
  if (worldOrigin_.initialized) {
    return;
  }

  const sim::FDMKinematicState &state = source.fdmState.state;
  const double latitudeRad = state.latitudeRad;
  const double longitudeRad = state.longitudeRad;
  const double radiusFt = state.altitudeAslFt;
  if (!std::isfinite(latitudeRad) || !std::isfinite(longitudeRad)
      || !std::isfinite(radiusFt)) {
    return;
  }

  worldOrigin_ = {
      .latitudeRad = latitudeRad,
      .longitudeRad = longitudeRad,
      .radiusFt = radiusFt,
      .initialized = true,
  };
}

Vec3 FlightVisualizer::ProjectWorldPosition(
    const sim::SimulationInstanceSnapshot &source) const {
  if (!worldOrigin_.initialized) {
    return {0.0F, 0.0F, AircraftOriginZ};
  }

  const sim::FDMKinematicState &state = source.fdmState.state;
  const double latitudeRad = state.latitudeRad;
  const double longitudeRad = state.longitudeRad;
  const double radiusFt = state.altitudeAslFt;
  if (!std::isfinite(latitudeRad) || !std::isfinite(longitudeRad)
      || !std::isfinite(radiusFt)) {
    return {0.0F, 0.0F, AircraftOriginZ};
  }

  const double northMeters =
      (latitudeRad - worldOrigin_.latitudeRad) * EarthRadiusMeters;
  const double eastMeters =
      math::WrapAngleRad(longitudeRad - worldOrigin_.longitudeRad)
      * std::cos(worldOrigin_.latitudeRad) * EarthRadiusMeters;
  const double altitudeDeltaFt = radiusFt - worldOrigin_.radiusFt;
  return {
      static_cast<float>(northMeters / MetersPerVizUnit),
      static_cast<float>(eastMeters / MetersPerVizUnit),
      AircraftOriginZ + static_cast<float>(altitudeDeltaFt / FeetPerVizUnit),
  };
}

void FlightVisualizer::SyncFlightPath(
    const sim::SimulationInstanceSnapshot &source) {
  flightPath_.AddSample(source.aircraft.simulationTimeSec,
      source.fdmState.state.latitudeRad,
      source.fdmState.state.longitudeRad);
}

void FlightVisualizer::SyncGroundScroll(const sim::AircraftState &state) {
  const double sampleTime = state.simulationTimeSec;
  if (!std::isfinite(sampleTime)) {
    motion_.hasSample = false;
    return;
  }

  if (!motion_.hasSample || sampleTime < motion_.lastSampleTimeSec) {
    motion_.lastSampleTimeSec = sampleTime;
    motion_.hasSample = true;
    motion_.groundScroll = {};
    return;
  }

  const double dt = sampleTime - motion_.lastSampleTimeSec;
  motion_.lastSampleTimeSec = sampleTime;
  if (dt <= 0.0 || dt > MaxMotionTickSec) {
    return;
  }

  const float speedMps = HorizontalSpeedMps(state);
  if (speedMps <= 0.0F || !std::isfinite(state.headingDeg)) {
    return;
  }

  const float distanceViz =
      static_cast<float>(dt) * speedMps / MetersPerVizUnit;
  const float headingRad = static_cast<float>(math::DegToRad(state.headingDeg));
  const Vec3 forward{std::cos(headingRad), std::sin(headingRad), 0.0F};

  motion_.groundScroll = motion_.groundScroll - forward * distanceViz;
}

void FlightVisualizer::UpdateSnapshotViewState() {
  snapshot_.viewMode = viewMode_;
  snapshot_.viewOptions = viewOptions_;
  snapshot_.groundScroll = motion_.groundScroll;
  snapshot_.shadowEnabled = shadowEnabled_;
}

void FlightVisualizer::BuildScene() {
  scene_.Clear();

  GameObject &cameraObject = scene_.CreateGameObject("Main Camera");
  mainCamera_ = &cameraObject.AddComponent<CameraComponent>();
  mainCamera_->SetEye({5.5F, -8.0F, 4.2F});
  mainCamera_->SetTarget({0.0F, 0.0F, 0.2F});
  mainCamera_->SetWorldUp({0.0F, 0.0F, 1.0F});
  FlightCameraController &cameraController =
      cameraObject.AddComponent<FlightCameraController>();
  cameraController.SetCamera(mainCamera_);

  GameObject &groundGrid = scene_.CreateGameObject("Ground Grid");
  groundGrid.GetTransform().SetPosition({0.0F, 0.0F, -0.9F});
  groundGrid.AddComponent<GroundGridRenderer>();

  GameObject &altitudeCue = scene_.CreateGameObject("Altitude Cue");
  altitudeCue.AddComponent<AltitudeCueRenderer>();

  GameObject &shadowAircraft = scene_.CreateGameObject("Shadow Aircraft");
  shadowAircraft.AddComponent<AircraftWireframeRenderer>(
      AircraftRenderStyle::Shadow);

  GameObject &aircraft = scene_.CreateGameObject("Aircraft");
  aircraft.AddComponent<AircraftWireframeRenderer>(AircraftRenderStyle::Main);

  GameObject &overlay = scene_.CreateGameObject("Telemetry Overlay");
  overlay.AddComponent<TelemetryOverlay>();
}
} // namespace viz
