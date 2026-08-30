#pragma once

#include "flightui/visualization/core/Math.hpp"
#include "flightui/visualization/render/CameraComponent.hpp"

#include <imgui.h>

#include <optional>

namespace viz {
class LineCanvas {
public:
  LineCanvas(ImDrawList &drawList, ImVec2 min, ImVec2 max, CameraView camera,
      float focalLength);

  // Canvas geometry
  ImDrawList &GetDrawList() const { return drawList_; }
  ImVec2 GetMin() const { return min_; }
  ImVec2 GetMax() const { return max_; }
  ImVec2 GetCenter() const { return center_; }

  // Drawing and projection
  void Fill(ImU32 color);
  void Border(ImU32 color, float thickness = 1.0F);
  void Line(Vec3 a, Vec3 b, ImU32 color, float thickness = 1.0F);
  std::optional<ImVec2> ProjectPoint(Vec3 point) const;

private:
  // Projection
  std::optional<ImVec2> Project(Vec3 point) const;

  // Render target
  ImDrawList &drawList_;

  // Canvas geometry
  ImVec2 min_;
  ImVec2 max_;
  ImVec2 center_;

  // Camera projection
  CameraView camera_;
  float focalLength_ = 1.0F;
};
} // namespace viz
