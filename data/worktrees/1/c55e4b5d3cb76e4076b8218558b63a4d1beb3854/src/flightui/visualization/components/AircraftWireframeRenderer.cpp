#include "flightui/visualization/components/AircraftWireframeRenderer.hpp"

#include "flightui/visualization/core/Transform.hpp"
#include "flightui/visualization/render/LineCanvas.hpp"

#include <array>

namespace {
struct Segment {
  viz::Vec3 a;
  viz::Vec3 b;
};

constexpr std::array AirframeSegments{
    Segment{{1.7F, 0.0F, 0.0F}, {-1.45F, 0.0F, 0.0F}},
    Segment{{0.15F, -2.25F, 0.0F}, {0.15F, 2.25F, 0.0F}},
    Segment{{-1.35F, -0.75F, 0.0F}, {-1.35F, 0.75F, 0.0F}},
    Segment{{-1.45F, 0.0F, 0.0F}, {-1.35F, 0.0F, 0.75F}},
    Segment{{-1.35F, 0.0F, 0.75F}, {-1.15F, 0.0F, 0.0F}},
    Segment{{1.7F, 0.0F, 0.0F}, {0.15F, -2.25F, 0.0F}},
    Segment{{1.7F, 0.0F, 0.0F}, {0.15F, 2.25F, 0.0F}},
    Segment{{-1.45F, 0.0F, 0.0F}, {0.15F, -2.25F, 0.0F}},
    Segment{{-1.45F, 0.0F, 0.0F}, {0.15F, 2.25F, 0.0F}},
    Segment{{-1.45F, 0.0F, 0.0F}, {-1.35F, -0.75F, 0.0F}},
    Segment{{-1.45F, 0.0F, 0.0F}, {-1.35F, 0.75F, 0.0F}},
    Segment{{-1.35F, -0.75F, 0.0F}, {-1.35F, 0.75F, 0.0F}},
};
} // namespace

namespace viz {
AircraftWireframeRenderer::AircraftWireframeRenderer(AircraftRenderStyle style)
    : style_(style) {}

void AircraftWireframeRenderer::OnTick(const TickContext &context) {
  const AircraftSnapshot &aircraft = style_ == AircraftRenderStyle::Shadow
                                         ? context.snapshot.shadowAircraft
                                         : context.snapshot.aircraft;
  const auto &state = aircraft.state;
  Transform &transform = GetTransform();
  transform.SetPosition(aircraft.position);
  transform.SetRotationDeg({
      -static_cast<float>(state.rollDeg),
      -static_cast<float>(state.pitchDeg),
      static_cast<float>(state.headingDeg),
  });
}

void AircraftWireframeRenderer::Render(RenderContext &context) const {
  const bool shadow = style_ == AircraftRenderStyle::Shadow;
  const AircraftSnapshot &aircraft =
      shadow ? context.snapshot.shadowAircraft : context.snapshot.aircraft;
  if (!aircraft.available || (shadow && !context.snapshot.shadowEnabled)) {
    return;
  }

  const Transform &transform = GetTransform();
  const ImU32 bodyColor =
      shadow ? IM_COL32(158, 166, 176, 72) : IM_COL32(238, 242, 248, 255);
  const float thickness = shadow ? 1.4F : 2.0F;

  for (const Segment &segment : AirframeSegments) {
    context.canvas.Line(transform.TransformPoint(segment.a),
        transform.TransformPoint(segment.b),
        bodyColor,
        thickness);
  }

  const Vec3 origin = transform.TransformPoint({0.0F, 0.0F, 0.0F});
  const ImU32 forwardColor =
      shadow ? IM_COL32(170, 176, 184, 72) : IM_COL32(248, 92, 92, 255);
  const ImU32 rightColor =
      shadow ? IM_COL32(150, 160, 170, 72) : IM_COL32(92, 210, 132, 255);
  const ImU32 upColor =
      shadow ? IM_COL32(180, 186, 194, 72) : IM_COL32(112, 168, 255, 255);
  context.canvas.Line(origin,
      transform.TransformPoint({2.3F, 0.0F, 0.0F}),
      forwardColor,
      thickness);
  context.canvas.Line(origin,
      transform.TransformPoint({0.0F, 1.35F, 0.0F}),
      rightColor,
      thickness);
  context.canvas.Line(origin,
      transform.TransformPoint({0.0F, 0.0F, 1.25F}),
      upColor,
      thickness);
}
} // namespace viz
