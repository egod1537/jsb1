#pragma once

#include "flightui/visualization/core/Component.hpp"
#include "flightui/visualization/core/Math.hpp"

namespace viz {
struct CameraView {
  Vec3 eye;
  Vec3 forward;
  Vec3 right;
  Vec3 up;
};

class CameraComponent final : public Component {
public:
  void SetEye(Vec3 eye) { eye_ = eye; }
  void SetTarget(Vec3 target) { target_ = target; }
  void SetWorldUp(Vec3 worldUp) { worldUp_ = worldUp; }

  CameraView BuildView() const;

private:
  Vec3 eye_{5.5F, -8.0F, 4.2F};
  Vec3 target_{0.0F, 0.0F, 0.2F};
  Vec3 worldUp_{0.0F, 0.0F, 1.0F};
};
} // namespace viz
