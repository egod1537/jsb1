#include "flightui/visualization/render/CameraComponent.hpp"

namespace viz {
CameraView CameraComponent::BuildView() const {
  CameraView view{};
  view.eye = eye_;
  view.forward = Normalize(target_ - eye_);
  view.right = Normalize(Cross(view.forward, worldUp_));
  view.up = Normalize(Cross(view.right, view.forward));
  return view;
}
} // namespace viz
