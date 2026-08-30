#include "flightui/visualization/render/LineCanvas.hpp"
#include "flightui/core/UIScale.hpp"

namespace {
constexpr float NearPlane = 0.1F;
}

namespace viz {
LineCanvas::LineCanvas(ImDrawList &drawList, ImVec2 min, ImVec2 max,
    CameraView camera, float focalLength)
    : drawList_(drawList), min_(min), max_(max),
      center_{min.x + (max.x - min.x) * 0.5F, min.y + (max.y - min.y) * 0.54F},
      camera_(camera), focalLength_(focalLength) {}

void LineCanvas::Fill(ImU32 color) {
  drawList_.AddRectFilled(min_, max_, color);
}

void LineCanvas::Border(ImU32 color, float thickness) {
  drawList_.AddRect(min_, max_, color, 0.0F, 0, FlightUI::Ui(thickness));
}

void LineCanvas::Line(Vec3 a, Vec3 b, ImU32 color, float thickness) {
  float depthA = Dot(a - camera_.eye, camera_.forward);
  float depthB = Dot(b - camera_.eye, camera_.forward);

  if (depthA <= NearPlane && depthB <= NearPlane) {
    return;
  }

  if (depthA <= NearPlane || depthB <= NearPlane) {
    const float t = (NearPlane - depthA) / (depthB - depthA);
    const Vec3 clippedPoint = a + (b - a) * t;
    if (depthA <= NearPlane) {
      a = clippedPoint;
      depthA = NearPlane;
    } else {
      b = clippedPoint;
      depthB = NearPlane;
    }
  }

  const auto projectedA = Project(a);
  const auto projectedB = Project(b);
  if (!projectedA.has_value() || !projectedB.has_value()) {
    return;
  }

  drawList_.AddLine(*projectedA, *projectedB, color, FlightUI::Ui(thickness));
}

std::optional<ImVec2> LineCanvas::ProjectPoint(Vec3 point) const {
  return Project(point);
}

std::optional<ImVec2> LineCanvas::Project(Vec3 point) const {
  const Vec3 toPoint = point - camera_.eye;
  const float depth = Dot(toPoint, camera_.forward);
  if (depth < NearPlane) {
    return std::nullopt;
  }

  const float x = Dot(toPoint, camera_.right);
  const float y = Dot(toPoint, camera_.up);
  const float scale = focalLength_ / depth;
  return ImVec2(center_.x + x * scale, center_.y - y * scale);
}
} // namespace viz
