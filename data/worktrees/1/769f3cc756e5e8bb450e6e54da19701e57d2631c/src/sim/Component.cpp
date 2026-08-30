#include "sim/Component.hpp"

#include "sim/Simulation.hpp"

namespace sim {
Aircraft &Component::GetAircraft() { return owner_->GetAircraft(); }

const Aircraft &Component::GetAircraft() const {
  const Simulation *owner = owner_;
  return owner->GetAircraft();
}

telemetry::TelemetryRegistry &Component::GetTelemetryRegistry() {
  return owner_->GetTelemetryRegistry();
}

const telemetry::TelemetryRegistry &Component::GetTelemetryRegistry() const {
  const Simulation *owner = owner_;
  return owner->GetTelemetryRegistry();
}

Component *Component::FindComponent(const std::type_info &type) {
  return owner_ != nullptr ? owner_->FindComponent(type) : nullptr;
}

const Component *Component::FindComponent(const std::type_info &type) const {
  const Simulation *owner = owner_;
  return owner != nullptr ? owner->FindComponent(type) : nullptr;
}
} // namespace sim
