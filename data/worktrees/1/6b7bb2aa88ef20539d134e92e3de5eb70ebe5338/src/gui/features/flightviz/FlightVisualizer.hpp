#pragma once

#include "gui/architecture/EventSink.hpp"
#include "gui/features/flightviz/FlightVizEvents.hpp"
#include "flightui/visualization/core/FrameSnapshot.hpp"
#include "flightui/visualization/core/FlightPathHistory.hpp"
#include "flightui/visualization/core/Scene.hpp"

#include <optional>

struct ImVec2;

namespace gui {
struct EditorIconHandle;
}

namespace sim {
struct SimulationInstanceSnapshot;
} // namespace sim

namespace viz {
class CameraComponent;

class FlightVisualizer {
public:
  // Lifetime
  FlightVisualizer();
  ~FlightVisualizer();

  // Frame update
  bool Tick(const sim::SimulationInstanceSnapshot *mainSnapshot,
      const sim::SimulationInstanceSnapshot *shadowSnapshot = nullptr);
  const FrameSnapshot &GetFrameSnapshot() const { return snapshot_; }

  // Visualization state
  ViewMode GetViewMode() const { return viewMode_; }
  void SetViewMode(ViewMode mode);
  bool IsShadowEnabled() const { return shadowEnabled_; }
  void SetShadowEnabled(bool enabled);
  const ViewOptions &GetViewOptions() const { return viewOptions_; }
  void SetViewOptions(const ViewOptions &options) { viewOptions_ = options; }
  void ClearFlightPath() { flightPath_.Reset(); }

  // Rendering
  void RenderScene(const gui::EditorIconHandle &shadowIcon,
      const gui::EditorIconHandle &viewOptionsIcon,
      const gui::EditorIconHandle &cameraViewIcon, const char *shadowTooltip,
      const char *unavailableMessage,
      gui::architecture::EventSink<gui::FlightVizShadowVisibilityChanged>
          shadowEvents,
      gui::architecture::EventSink<gui::FlightVizCameraViewToggleRequested>
          cameraEvents,
      gui::architecture::EventSink<gui::FlightVizDisplayOptionsChanged>
          displayEvents,
      gui::architecture::EventSink<gui::FlightVizClearPathRequested>
          pathEvents);

private:
  // Scene setup and interaction
  void BuildScene();
  void HandleInput(
      gui::architecture::EventSink<gui::FlightVizCameraViewToggleRequested>
          cameraEvents);
  void RenderToolbar(const gui::EditorIconHandle &shadowIcon,
      const gui::EditorIconHandle &viewOptionsIcon,
      const gui::EditorIconHandle &cameraViewIcon, const char *shadowTooltip,
      gui::architecture::EventSink<gui::FlightVizShadowVisibilityChanged>
          shadowEvents,
      gui::architecture::EventSink<gui::FlightVizCameraViewToggleRequested>
          cameraEvents,
      gui::architecture::EventSink<gui::FlightVizDisplayOptionsChanged>
          displayEvents,
      gui::architecture::EventSink<gui::FlightVizClearPathRequested>
          pathEvents);
  void RenderViewOptionsPopup(
      gui::architecture::EventSink<gui::FlightVizDisplayOptionsChanged>
          displayEvents,
      gui::architecture::EventSink<gui::FlightVizClearPathRequested>
          pathEvents);
  void RenderMinimap(ImVec2 min, ImVec2 max);

  // Aircraft synchronization
  void ResetMainState();
  void UpdateWorldOrigin(const sim::SimulationInstanceSnapshot &source);
  AircraftSnapshot CaptureAircraft(
      const sim::SimulationInstanceSnapshot &source) const;
  Vec3 ProjectWorldPosition(
      const sim::SimulationInstanceSnapshot &source) const;
  void SyncFlightPath(const sim::SimulationInstanceSnapshot &source);
  void SyncGroundScroll(const sim::AircraftState &state);
  void UpdateSnapshotViewState();

  struct MotionState {
    Vec3 groundScroll{};
    double lastSampleTimeSec = 0.0;
    bool hasSample = false;
  };

  struct WorldOrigin {
    double latitudeRad = 0.0;
    double longitudeRad = 0.0;
    double radiusFt = 0.0;
    bool initialized = false;
  };

  // Scene state
  Scene scene_;
  CameraComponent *mainCamera_ = nullptr;
  FrameSnapshot snapshot_{};

  // View state
  ViewMode viewMode_ = ViewMode::Orbit;
  ViewOptions viewOptions_{};
  bool shadowEnabled_ = false;
  bool minimapMinimized_ = false;

  // Flight path
  FlightPathHistory flightPath_{};

  // Motion cache
  MotionState motion_{};

  // Fixed local-world projection
  WorldOrigin worldOrigin_{};
  std::optional<double> lastMainSimulationTimeSec_;
};
} // namespace viz
