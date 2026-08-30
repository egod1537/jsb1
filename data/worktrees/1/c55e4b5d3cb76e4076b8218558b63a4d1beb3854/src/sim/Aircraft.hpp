#pragma once

#include "sim/jsbsim/ControlSystem.hpp"
#include "sim/jsbsim/EngineSystem.hpp"
#include "sim/jsbsim/FDMStateAccess.hpp"
#include "sim/jsbsim/Properties.hpp"
#include "sim/AircraftState.hpp"
#include "sim/FDMState.hpp"
#include "sim/InitialCondition.hpp"
#include "sim/SimulationConfig.h"

#include <filesystem>
#include <memory>

namespace JSBSim {
class FGFDMExec;
} // namespace JSBSim

namespace gnc {
enum class TrimMode;
struct TrimRequest;
struct LinearizationResult;
} // namespace gnc

namespace sim {
class Aircraft {
public:
  // Lifetime and stepping
  Aircraft();
  ~Aircraft();
  Aircraft(const Aircraft &other) = delete;
  Aircraft &operator=(const Aircraft &other) = delete;
  bool Initialize(const SimulationConfig &config,
      const InitialCondition &initialCondition);
  bool Tick();
  bool Step(double dtSec);

  // Configuration
  const SimulationConfig &GetConfig() const;

  // Initial condition and reset
  bool ApplyInitialCondition(const InitialCondition &initialCondition);
  void SetInitialConditionInputs(const InitialCondition &initialCondition);
  InitialCondition GetCurrentCondition() const;
  bool Reset(const SimulationConfig &config,
      const InitialCondition &initialCondition);
  void ResetSimulationTime();

  // Trim operations
  bool InitializeForTrim(const gnc::TrimRequest &request);
  void RunTrim(gnc::TrimMode mode);

  // Aircraft state
  AircraftState GetAircraftState() const;
  AircraftStateDerivative GetAircraftStateDerivative() const;

  // Flight-dynamics state synchronization
  FDMState ExtractFDMState(FDMStateFlags flags) const;
  void ApplyFDMState(const FDMState &state);
  void SetIntegrationSuspended(bool suspended);
  bool IsIntegrationSuspended() const;

  // Linearization
  gnc::LinearizationResult ComputeLinearization();

  // Flight model interfaces
  jsbsim::ControlSystem &GetControls();
  const jsbsim::ControlSystem &GetControls() const;
  jsbsim::EngineSystem &GetEngines();
  const jsbsim::EngineSystem &GetEngines() const;
  jsbsim::Properties &GetProperties();
  const jsbsim::Properties &GetProperties() const;

private:
  // JSBSim setup
  void ConfigurePaths();
  void ConfigureOutputPath();
  void RemoveOutputDirectory();
  bool LoadAircraft(const SimulationConfig &config);
  void ConfigureSimulation(const SimulationConfig &config);
  void DisableExternalOutput();
  void PrepareExternalOutputForReset();
  bool InitializeState();

  // Configuration
  SimulationConfig config_;
  std::filesystem::path outputDirectory_;

  // JSBSim dependencies
  std::unique_ptr<JSBSim::FGFDMExec> fdm_;
  jsbsim::FDMStateAccess fdmStateAccess_;
  jsbsim::ControlSystem controls_;
  jsbsim::EngineSystem engines_;
  jsbsim::Properties properties_;
};
} // namespace sim
