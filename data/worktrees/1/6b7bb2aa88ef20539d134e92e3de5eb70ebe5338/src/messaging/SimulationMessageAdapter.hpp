#pragma once

#include "messaging/MessageBus.hpp"

#include <cstdint>
#include <vector>

namespace sim {
class SimulationRuntime;
}

namespace application::messaging {
class SimulationMessageAdapter final {
public:
  SimulationMessageAdapter(MessageBus &bus, sim::SimulationRuntime &runtime);

  // Runtime output publication
  void PublishState();
  void PublishTelemetry();

private:
  std::string GetRuntimeError(std::string fallback) const;

  // Dependencies
  MessageBus &bus_;
  sim::SimulationRuntime &runtime_;

  // Command subscription lifetime
  std::vector<Subscription> subscriptions_;

  // Published telemetry versions
  std::uint64_t primaryTelemetryVersion_ = 0;
  std::uint64_t baselineTelemetryVersion_ = 0;
};
} // namespace application::messaging
