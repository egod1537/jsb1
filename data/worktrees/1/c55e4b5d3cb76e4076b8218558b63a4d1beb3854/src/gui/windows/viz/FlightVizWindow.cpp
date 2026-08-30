#include "gui/windows/viz/FlightVizWindow.hpp"

#include "gui/resources/EditorIcon.hpp"
#include "gui/resources/EditorIconRegistry.hpp"

namespace {
const char *GetWindowTitle(sim::SimulationSlot slot) {
  return slot == sim::SimulationSlot::Primary ? "Flight Viz · Primary"
                                              : "Flight Viz · Baseline";
}

const char *ResolveWindowId(sim::SimulationSlot slot) {
  return slot == sim::SimulationSlot::Primary ? "FlightVizPrimary"
                                              : "FlightVizBaseline";
}

const char *GetUnavailableMessage(sim::SimulationSlot slot) {
  return slot == sim::SimulationSlot::Primary
             ? "Primary simulation unavailable"
             : "Baseline simulation unavailable";
}

const char *GetShadowTooltip(sim::SimulationSlot slot) {
  return slot == sim::SimulationSlot::Primary ? "Show Baseline Shadow"
                                              : "Show Primary Shadow";
}
} // namespace

namespace gui {
FlightVizWindow::FlightVizWindow(sim::SimulationSlot slot,
    EditorIconRegistry *icons)
    : Window(GetWindowTitle(slot), EditorIconAliases::FlightViz,
          ResolveWindowId(slot)),
      slot_(slot), icons_(icons) {}

ImGuiWindowFlags FlightVizWindow::GetWindowFlags() const {
  return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void FlightVizWindow::OnRender(const sim::SimulationSnapshot &snapshot) {
  const sim::SimulationInstanceSnapshot *primary =
      snapshot.primary.available ? &snapshot.primary : nullptr;
  const sim::SimulationInstanceSnapshot *baseline =
      snapshot.baseline.has_value() && snapshot.baseline->available
          ? &*snapshot.baseline
          : nullptr;
  const bool primarySlot = slot_ == sim::SimulationSlot::Primary;
  visualizer_.Tick(primarySlot ? primary : baseline,
      primarySlot ? baseline : primary);
  const EditorIconHandle shadowIcon =
      icons_ != nullptr ? icons_->Get(EditorIconAliases::ShadowAircraft)
                        : EditorIconHandle{};
  const EditorIconHandle viewOptionsIcon =
      icons_ != nullptr ? icons_->Get(EditorIconAliases::ViewOptions)
                        : EditorIconHandle{};
  const EditorIconHandle cameraViewIcon =
      icons_ != nullptr ? icons_->Get(EditorIconAliases::CameraView)
                        : EditorIconHandle{};
  visualizer_.RenderScene(shadowIcon,
      viewOptionsIcon,
      cameraViewIcon,
      GetShadowTooltip(slot_),
      GetUnavailableMessage(slot_),
      architecture::EventSink<FlightVizShadowVisibilityChanged>{
          [this](const FlightVizShadowVisibilityChanged &event) {
            Handle(event);
          }},
      architecture::EventSink<FlightVizCameraViewToggleRequested>{
          [this](const FlightVizCameraViewToggleRequested &event) {
            Handle(event);
          }},
      architecture::EventSink<FlightVizDisplayOptionsChanged>{
          [this](
              const FlightVizDisplayOptionsChanged &event) { Handle(event); }},
      architecture::EventSink<FlightVizClearPathRequested>{
          [this](const FlightVizClearPathRequested &event) { Handle(event); }});
}

void FlightVizWindow::Handle(const FlightVizShadowVisibilityChanged &event) {
  visualizer_.SetShadowEnabled(event.enabled);
}

void FlightVizWindow::Handle(const FlightVizCameraViewToggleRequested &) {
  const viz::ViewMode next = visualizer_.GetViewMode() == viz::ViewMode::Orbit
                                 ? viz::ViewMode::ThirdPerson
                                 : viz::ViewMode::Orbit;
  visualizer_.SetViewMode(next);
}

void FlightVizWindow::Handle(const FlightVizDisplayOptionsChanged &event) {
  visualizer_.SetViewOptions({
      .showGroundGrid = event.showGroundGrid,
      .showTelemetry = event.showTelemetry,
      .showMinimap = event.showMinimap,
  });
}

void FlightVizWindow::Handle(const FlightVizClearPathRequested &) {
  visualizer_.ClearFlightPath();
}
} // namespace gui
