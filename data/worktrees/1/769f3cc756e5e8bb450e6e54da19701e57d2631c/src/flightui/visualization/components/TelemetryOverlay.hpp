#pragma once

#include "flightui/visualization/core/Component.hpp"

namespace viz {
class TelemetryOverlay final : public Component {
public:
  void Render(RenderContext &context) const override;
};
} // namespace viz
