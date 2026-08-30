#include "flightui/visualization/components/FlightCameraController.hpp"
#include "gui/features/flightviz/FlightVisualizer.hpp"

#include "flightui/visualization/render/CameraComponent.hpp"

#include <cassert>

namespace {
constexpr viz::Vec3 AircraftPosition{0.0F, 0.0F, 0.35F};

void RequireAircraftTracked(float visualAltitude) {
  viz::CameraComponent camera;
  viz::FlightCameraController controller;
  controller.SetCamera(&camera);

  viz::FrameSnapshot snapshot{};
  snapshot.viewMode = viz::ViewMode::ThirdPerson;
  snapshot.aircraft.available = true;
  snapshot.aircraft.position = AircraftPosition;
  snapshot.aircraft.visualAltitude = visualAltitude;
  snapshot.aircraft.state.headingDeg = 37.0;
  snapshot.aircraft.state.pitchDeg = 5.0;
  snapshot.shadowEnabled = true;
  snapshot.shadowAircraft.available = true;
  snapshot.shadowAircraft.position = {1000.0F, 1000.0F, 1000.0F};
  controller.OnTick(viz::TickContext{snapshot});

  const viz::CameraView view = camera.BuildView();
  const viz::Vec3 directionToAircraft =
      viz::Normalize(AircraftPosition - view.eye);

  assert(viz::Dot(view.forward, directionToAircraft) > 0.98F);
}

void RequireVisualizerViewModesAreIndependent() {
  viz::FlightVisualizer primary;
  viz::FlightVisualizer baseline;
  assert(primary.GetViewMode() == viz::ViewMode::Orbit);
  assert(baseline.GetViewMode() == viz::ViewMode::Orbit);

  primary.SetViewMode(viz::ViewMode::ThirdPerson);
  assert(primary.GetViewMode() == viz::ViewMode::ThirdPerson);
  assert(baseline.GetViewMode() == viz::ViewMode::Orbit);
}
} // namespace

int main() {
  RequireAircraftTracked(0.35F);
  RequireAircraftTracked(52.0F);
  RequireVisualizerViewModesAreIndependent();
  return 0;
}
