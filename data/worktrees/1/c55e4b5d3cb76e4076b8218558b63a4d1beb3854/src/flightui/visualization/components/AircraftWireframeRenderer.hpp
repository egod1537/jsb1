#pragma once

#include "flightui/visualization/core/Component.hpp"

namespace viz {
enum class AircraftRenderStyle {
  Main,
  Shadow,
};

class AircraftWireframeRenderer final : public Component {
public:
  explicit AircraftWireframeRenderer(
      AircraftRenderStyle style = AircraftRenderStyle::Main);

  void OnTick(const TickContext &context) override;
  void Render(RenderContext &context) const override;

private:
  AircraftRenderStyle style_;
};
} // namespace viz
