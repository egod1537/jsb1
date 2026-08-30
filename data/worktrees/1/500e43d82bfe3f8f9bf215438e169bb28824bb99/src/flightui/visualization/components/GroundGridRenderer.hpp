#pragma once

#include "flightui/visualization/core/Component.hpp"

namespace viz {
class GroundGridRenderer final : public Component {
public:
  void OnTick(const TickContext &context) override;
  void Render(RenderContext &context) const override;

private:
  bool hasVisualAltitude_ = false;
  float visualAltitude_ = 1.0F;
  float extent_ = 70.0F;
  float spacing_ = 1.0F;
  float baseSpacing_ = 1.0F;
  int maxLinesPerAxis_ = 88;
  int majorLineInterval_ = 5;
};
} // namespace viz
