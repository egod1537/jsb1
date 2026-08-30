#pragma once

#include "gui/Component.hpp"

#include <vector>

namespace gui {
class DeclarativeDemoWindow final : public Component {
public:
  DeclarativeDemoWindow();
  ~DeclarativeDemoWindow() override;

protected:
  void OnTick(const GUIFrameContext &context) override;

private:
  void AppendTelemetrySample();

  // Control state
  bool autopilotEnabled_ = false;
  double throttle_ = 0.5;
  double elevator_ = 0.0;

  // Telemetry history
  double lastSampleTime_ = -1.0;
  std::vector<double> timeHistory_;
  std::vector<double> throttleHistory_;
  std::vector<double> elevatorHistory_;
};
} // namespace gui
