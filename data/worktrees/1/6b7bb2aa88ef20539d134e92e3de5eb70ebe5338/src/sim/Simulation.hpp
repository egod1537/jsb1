#pragma once

#include "sim/Aircraft.hpp"
#include "sim/Component.hpp"
#include "sim/ErrorTracker.hpp"
#include "sim/InitialCondition.hpp"
#include "sim/SimulationConfig.h"
#include "sim/Tick.hpp"
#include "sim/control/FlightControlManager.hpp"
#include "sim/gnc/TrimService.hpp"
#include "sim/telemetry/TelemetryRegistry.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <typeinfo>
#include <vector>

namespace gnc {
class IAutopilot;
} // namespace gnc

namespace sim {
struct SimulationResetOptions {
  bool runTrim = true;
  gnc::TrimMode trimMode = gnc::TrimMode::Full;
  std::optional<FDMEnvironmentState> environment;
};

class Simulation {
public:
  // Lifetime and stepping
  explicit Simulation(std::unique_ptr<gnc::IAutopilot> autopilot);
  ~Simulation();

  Simulation(const Simulation &other) = delete;
  Simulation &operator=(const Simulation &other) = delete;

  bool Initialize(const SimulationConfig &config);
  bool Tick();
  bool Step(double dtSec);
  void Shutdown();
  bool IsInitialized() const { return initialized_; }

  // Configuration
  const SimulationConfig &GetConfig() const;
  double GetTickSizeSec() const;
  double GetTime() const;

  // Initial condition
  bool Reset();
  bool Reset(const InitialCondition &initialCondition);
  bool Reset(const InitialCondition &initialCondition,
      const SimulationResetOptions &options);
  InitialCondition GetCurrentCondition() const;
  const InitialCondition &GetDefaultInitialCondition() const;

  // Trim
  gnc::TrimService &GetTrimService();
  const gnc::TrimService &GetTrimService() const;

  // Aircraft
  Aircraft &GetAircraft();
  const Aircraft &GetAircraft() const;

  // Telemetry
  telemetry::TelemetryRegistry &GetTelemetryRegistry();
  const telemetry::TelemetryRegistry &GetTelemetryRegistry() const;
  telemetry::TelemetryRegistry &GetTelemetry();
  const telemetry::TelemetryRegistry &GetTelemetry() const;

  // Components
  template <typename T, typename... Args> T *AddComponent(Args &&...args);
  template <typename T> T *GetComponent();
  template <typename T> const T *GetComponent() const;
  template <typename T> bool RemoveComponent();

  // Diagnostics
  ErrorTracker &GetErrorTracker();
  const ErrorTracker &GetErrorTracker() const;

private:
  // Tick processing
  bool ProcessStep(double dtSec);
  sim::Tick MakeTick(double dtSec) const;
  void PublishAutopilotTelemetry(const sim::Tick &tick);
  void PublishAircraftTelemetry(const sim::Tick &tick);

  // Initial condition
  bool ApplyInitialTrim(const InitialCondition &initialCondition,
      gnc::TrimMode mode = gnc::TrimMode::Full);
  void ApplyEnvironment(const FDMEnvironmentState &environment);

  // Components
  bool InitializeComponent(Component &component);
  bool InitializeComponents();
  bool ResetComponents();
  bool RunPreTickComponents(const sim::Tick &tick);
  bool TickComponents(const sim::Tick &tick);
  bool RunPostTickComponents(const sim::Tick &tick);
  void ShutdownComponents();
  Component *FindComponent(const std::type_info &type);
  const Component *FindComponent(const std::type_info &type) const;

  // Configuration
  SimulationConfig config_;

  // Runtime state
  bool initialized_ = false;

  // Initial condition
  InitialCondition defaultInitialCondition_;

  // Trim state
  gnc::TrimService trimService_;

  // Simulation clock
  std::uint64_t tickIndex_ = 0;

  // Aircraft state
  Aircraft aircraft_;

  // Telemetry
  telemetry::TelemetryRegistry telemetryRegistry_;

  // Components
  std::vector<std::unique_ptr<Component>> components_;

  // Diagnostics
  ErrorTracker errorTracker_;

  friend class Component;
};
} // namespace sim

#include "sim/Simulation.inl"
