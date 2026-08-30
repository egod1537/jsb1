#include "flightui/visualization/components/FlightCameraController.hpp"

#include "flightui/visualization/render/CameraComponent.hpp"
#include "common/math/Math.hpp"

#include <algorithm>
namespace {
viz::Vec3 AircraftForward(const sim::AircraftState &state) {
  viz::Vec3 forward{1.0F, 0.0F, 0.0F};
  forward = viz::RotateY(forward,
      -static_cast<float>(math::DegToRad(state.pitchDeg)));
  forward = viz::RotateZ(forward,
      static_cast<float>(math::DegToRad(state.headingDeg)));
  return viz::Normalize(forward);
}
} // namespace

namespace viz {
void FlightCameraController::OnTick(const TickContext &context) {
  if (camera_ == nullptr) {
    return;
  }

  switch (context.snapshot.viewMode) {
  case ViewMode::ThirdPerson:
    ApplyThirdPersonCamera(context);
    break;
  case ViewMode::Orbit:
  default:
    ApplyOrbitCamera(context);
    break;
  }
}

void FlightCameraController::ApplyOrbitCamera(
    const TickContext &context) const {
  const AircraftSnapshot &aircraft = context.snapshot.aircraft;
  const float altitude = std::max(aircraft.visualAltitude, 0.35F);
  const float pullBack = std::min(altitude * 0.18F, 8.0F);
  const float lift = std::min(altitude * 0.10F, 8.0F);
  const float lookDown = std::min(altitude * 0.36F, 18.0F);

  camera_->SetEye(aircraft.position
                  + Vec3{5.5F + pullBack * 0.35F,
                      -8.0F - pullBack,
                      3.85F + lift});
  camera_->SetTarget(aircraft.position + Vec3{0.0F, 0.0F, -lookDown});
  camera_->SetWorldUp({0.0F, 0.0F, 1.0F});
}

void FlightCameraController::ApplyThirdPersonCamera(
    const TickContext &context) const {
  const AircraftSnapshot &aircraft = context.snapshot.aircraft;
  const Vec3 forward = AircraftForward(aircraft.state);
  const float altitude = std::max(aircraft.visualAltitude, 0.35F);
  const float chaseDistance = 7.0F + std::min(altitude * 0.22F, 9.0F);
  const Vec3 eye = aircraft.position - forward * chaseDistance
                   + Vec3{0.0F, 0.0F, 2.4F + std::min(altitude * 0.06F, 4.0F)};
  const Vec3 target = aircraft.position + forward * 4.0F;

  camera_->SetEye(eye);
  camera_->SetTarget(target);
  camera_->SetWorldUp({0.0F, 0.0F, 1.0F});
}

} // namespace viz
