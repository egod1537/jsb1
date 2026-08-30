#pragma once

#include <type_traits>
#include <typeinfo>

namespace sim {
class Aircraft;
class Simulation;
struct Tick;
} // namespace sim

namespace telemetry {
class TelemetryRegistry;
}

namespace sim {
class Component {
public:
  virtual ~Component() = default;

protected:
  // Lifecycle
  virtual bool OnInitialize() { return true; }
  virtual bool OnReset() { return true; }
  virtual bool OnPreTick(const Tick &tick) { return true; }
  virtual bool OnTick(const Tick &tick) { return true; }
  virtual bool OnPostTick(const Tick &tick) { return true; }
  virtual void OnShutdown() {}

  // Owner-provided dependencies
  Aircraft &GetAircraft();
  const Aircraft &GetAircraft() const;
  telemetry::TelemetryRegistry &GetTelemetryRegistry();
  const telemetry::TelemetryRegistry &GetTelemetryRegistry() const;

  template <typename T> T *GetComponent() {
    static_assert(std::is_base_of_v<Component, T>,
        "T must inherit from sim::Component");
    return static_cast<T *>(FindComponent(typeid(T)));
  }

  template <typename T> const T *GetComponent() const {
    static_assert(std::is_base_of_v<Component, T>,
        "T must inherit from sim::Component");
    return static_cast<const T *>(FindComponent(typeid(T)));
  }

private:
  // Owner routing
  Component *FindComponent(const std::type_info &type);
  const Component *FindComponent(const std::type_info &type) const;

  // Ownership and lifecycle state
  Simulation *owner_ = nullptr;
  bool initialized_ = false;

  friend class Simulation;
};
} // namespace sim
