#pragma once

#include "flightui/visualization/core/Math.hpp"
#include "common/math/Math.hpp"

namespace viz {
class Transform {
public:
  Vec3 &Position() { return position_; }
  const Vec3 &Position() const { return position_; }
  void SetPosition(Vec3 position) { position_ = position; }

  Vec3 &RotationDeg() { return rotationDeg_; }
  const Vec3 &RotationDeg() const { return rotationDeg_; }
  void SetRotationDeg(Vec3 rotationDeg) { rotationDeg_ = rotationDeg; }

  Vec3 &Scale() { return scale_; }
  const Vec3 &Scale() const { return scale_; }
  void SetScale(Vec3 scale) { scale_ = scale; }

  Vec3 TransformPoint(Vec3 localPoint) const {
    Vec3 worldPoint = localPoint * scale_;
    worldPoint =
        RotateX(worldPoint, static_cast<float>(math::DegToRad(rotationDeg_.x)));
    worldPoint =
        RotateY(worldPoint, static_cast<float>(math::DegToRad(rotationDeg_.y)));
    worldPoint =
        RotateZ(worldPoint, static_cast<float>(math::DegToRad(rotationDeg_.z)));
    return worldPoint + position_;
  }

private:
  Vec3 position_{};
  Vec3 rotationDeg_{};
  Vec3 scale_{1.0F, 1.0F, 1.0F};
};
} // namespace viz
