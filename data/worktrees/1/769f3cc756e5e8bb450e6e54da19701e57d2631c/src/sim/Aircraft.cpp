#include "sim/Aircraft.hpp"
#include "sim/gnc/TrimTypes.hpp"
#include "sim/linearization/LinearizationResult.hpp"
#include "common/math/Math.hpp"

#include <FGFDMExec.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <initialization/FGInitialCondition.h>
#include <initialization/FGLinearization.h>
#include <initialization/FGTrim.h>
#include <iostream>
#include <models/FGOutput.h>
#include <simgear/misc/sg_path.hxx>
#include <string>
#include <utility>

namespace {
constexpr const char *CurrentAltitudeAslFt = "position/h-sl-ft";
constexpr const char *CurrentLatitudeRad = "position/lat-gc-rad";
constexpr const char *CurrentLongitudeRad = "position/long-gc-rad";
constexpr const char *CurrentRollRad = "attitude/phi-rad";
constexpr const char *CurrentPitchRad = "attitude/theta-rad";
constexpr const char *CurrentHeadingRad = "attitude/psi-rad";
constexpr const char *CurrentPRadPerSec = "velocities/p-rad_sec";
constexpr const char *CurrentQRadPerSec = "velocities/q-rad_sec";
constexpr const char *CurrentRRadPerSec = "velocities/r-rad_sec";

bool IsFiniteInitialCondition(const sim::InitialCondition &initialCondition) {
  return std::isfinite(initialCondition.latitudeDeg)
         && std::isfinite(initialCondition.longitudeDeg)
         && std::isfinite(initialCondition.altitudeFt)
         && std::isfinite(initialCondition.rollDeg)
         && std::isfinite(initialCondition.pitchDeg)
         && std::isfinite(initialCondition.headingDeg)
         && std::isfinite(initialCondition.airspeedKts)
         && std::isfinite(initialCondition.pRadPerSec)
         && std::isfinite(initialCondition.qRadPerSec)
         && std::isfinite(initialCondition.rRadPerSec);
}

std::string ResolveJSBSimRootPath() {
  const std::filesystem::path cwd = std::filesystem::current_path();
  const std::filesystem::path candidates[] = {
      cwd / "build" / "_deps" / "jsbsim-src",
      cwd / "build" / "debug" / "_deps" / "jsbsim-src",
      cwd / "_deps" / "jsbsim-src",
      cwd.parent_path() / "_deps" / "jsbsim-src",
  };

  for (const auto &candidate : candidates) {
    if (std::filesystem::exists(candidate / "aircraft")
        && std::filesystem::exists(candidate / "engine")) {
      return candidate.generic_string();
    }
  }

  return candidates[0].generic_string();
}

int ToJSBTrimMode(gnc::TrimMode mode) {
  switch (mode) {
  case gnc::TrimMode::Longitudinal:
    return JSBSim::tLongitudinal;
  case gnc::TrimMode::Full:
    return JSBSim::tFull;
  case gnc::TrimMode::Ground:
    return JSBSim::tGround;
  }

  return JSBSim::tNone;
}
} // namespace

namespace sim {
Aircraft::Aircraft()
    : fdm_(std::make_unique<JSBSim::FGFDMExec>()), fdmStateAccess_(*fdm_),
      controls_(*fdm_), engines_(*fdm_), properties_(*fdm_) {}

Aircraft::~Aircraft() {
  fdm_.reset();
  RemoveOutputDirectory();
}

bool Aircraft::Initialize(const SimulationConfig &config,
    const InitialCondition &initialCondition) {
  config_ = config;
  controls_.SetInput({});

  ConfigurePaths();
  if (!LoadAircraft(config)) {
    return false;
  }

  ConfigureSimulation(config);
  SetInitialConditionInputs(initialCondition);
  return InitializeState();
}

bool Aircraft::Tick() { return Step(config_.GetDT()); }

bool Aircraft::Step(double dtSec) {
  fdm_->Setdt(dtSec);
  DisableExternalOutput();
  controls_.Apply();
  return fdm_->Run();
}

const SimulationConfig &Aircraft::GetConfig() const { return config_; }

AircraftState Aircraft::GetAircraftState() const {
  AircraftState state{};
  state.simulationTimeSec = properties_.SimTime().Sec();
  state.altitudeAglFt = properties_.AltitudeAgl().Ft();
  state.calibratedAirspeedKts = properties_.CalibratedAirspeed().Kts();
  state.trueAirspeedMps = properties_.TrueAirspeed().Mps();
  state.rollDeg = properties_.Roll().Deg();
  state.pitchDeg = properties_.Pitch().Deg();
  state.headingDeg = math::RadToDeg(properties_.Get(CurrentHeadingRad));
  state.courseDeg = math::Wrap(properties_.Course().Deg(), 0.0, 360.0);
  state.alphaDeg = properties_.Alpha().Deg();
  state.betaDeg = properties_.Beta().Deg();
  state.uMps = properties_.U().Mps();
  state.vMps = properties_.V().Mps();
  state.wMps = properties_.W().Mps();
  state.pDegPerSec = properties_.P().DegPerSec();
  state.qDegPerSec = properties_.Q().DegPerSec();
  state.rDegPerSec = properties_.R().DegPerSec();
  return state;
}

AircraftStateDerivative Aircraft::GetAircraftStateDerivative() const {
  AircraftStateDerivative derivative{};
  derivative.uDotMps2 = properties_.U().DotMps2();
  derivative.vDotMps2 = properties_.V().DotMps2();
  derivative.wDotMps2 = properties_.W().DotMps2();
  derivative.pDotDegPerSec2 = properties_.P().DotDegPerSec2();
  derivative.qDotDegPerSec2 = properties_.Q().DotDegPerSec2();
  derivative.rDotDegPerSec2 = properties_.R().DotDegPerSec2();
  return derivative;
}

FDMState Aircraft::ExtractFDMState(FDMStateFlags flags) const {
  return fdmStateAccess_.Extract(flags);
}

void Aircraft::ApplyFDMState(const FDMState &state) {
  fdmStateAccess_.Apply(state);

  if (HasFDMStateFlag(state.flags, FDMStateFlags::Controls)) {
    controls_.SetInput({
        .elevator = state.controls.elevatorCommand,
        .aileron = state.controls.aileronCommand,
        .rudder = state.controls.rudderCommand,
        .throttle = state.controls.throttleCommands.empty()
                        ? 0.0
                        : state.controls.throttleCommands.front(),
    });
  }
}

void Aircraft::SetIntegrationSuspended(bool suspended) {
  if (suspended)
    fdm_->SuspendIntegration();
  else
    fdm_->ResumeIntegration();
}

bool Aircraft::IsIntegrationSuspended() const {
  return fdm_->IntegrationSuspended();
}

gnc::LinearizationResult Aircraft::ComputeLinearization() {
  JSBSim::FGLinearization linearization(fdm_.get());

  const auto &jsbA = linearization.GetSystemMatrix();
  const auto &jsbB = linearization.GetInputMatrix();

  gnc::LinearizationResult result{};

  result.A.resize(jsbA.size(), jsbA.empty() ? 0 : jsbA[0].size());
  for (std::size_t i = 0; i < jsbA.size(); ++i) {
    for (std::size_t j = 0; j < jsbA[i].size(); ++j) {
      result.A(i, j) = jsbA[i][j];
    }
  }

  result.B.resize(jsbB.size(), jsbB.empty() ? 0 : jsbB[0].size());
  for (std::size_t i = 0; i < jsbB.size(); ++i) {
    for (std::size_t j = 0; j < jsbB[i].size(); ++j) {
      result.B(i, j) = jsbB[i][j];
    }
  }

  const auto &jsbX0 = linearization.GetInitialState();
  const auto &jsbU0 = linearization.GetInitialInput();

  result.x0.resize(jsbX0.size());
  for (std::size_t i = 0; i < jsbX0.size(); i++) {
    result.x0(i) = jsbX0[i];
  }

  result.u0.resize(jsbU0.size());
  for (std::size_t i = 0; i < jsbU0.size(); i++) {
    result.u0(i) = jsbU0[i];
  }

  result.stateNames = linearization.GetStateNames();
  result.inputNames = linearization.GetInputNames();

  return result;
}

bool Aircraft::ApplyInitialCondition(const InitialCondition &initialCondition) {
  if (!IsFiniteInitialCondition(initialCondition)) {
    return false;
  }

  SetInitialConditionInputs(initialCondition);
  PrepareExternalOutputForReset();
  return fdm_->RunIC();
}

void Aircraft::SetInitialConditionInputs(
    const InitialCondition &initialCondition) {
  auto ic = fdm_->GetIC();

  ic->SetLatitudeDegIC(initialCondition.latitudeDeg);
  ic->SetLongitudeDegIC(initialCondition.longitudeDeg);
  ic->SetAltitudeASLFtIC(initialCondition.altitudeFt);

  ic->SetPhiDegIC(initialCondition.rollDeg);
  ic->SetThetaDegIC(initialCondition.pitchDeg);
  ic->SetPsiDegIC(initialCondition.headingDeg);

  ic->SetVcalibratedKtsIC(initialCondition.airspeedKts);

  ic->SetPRadpsIC(initialCondition.pRadPerSec);
  ic->SetQRadpsIC(initialCondition.qRadPerSec);
  ic->SetRRadpsIC(initialCondition.rRadPerSec);
}

InitialCondition Aircraft::GetCurrentCondition() const {
  InitialCondition initialCondition{};
  initialCondition.latitudeDeg =
      math::RadToDeg(properties_.Get(CurrentLatitudeRad));
  initialCondition.longitudeDeg =
      math::RadToDeg(properties_.Get(CurrentLongitudeRad));
  initialCondition.altitudeFt = properties_.Get(CurrentAltitudeAslFt);
  initialCondition.rollDeg = math::RadToDeg(properties_.Get(CurrentRollRad));
  initialCondition.pitchDeg = math::RadToDeg(properties_.Get(CurrentPitchRad));
  initialCondition.headingDeg =
      math::RadToDeg(properties_.Get(CurrentHeadingRad));
  initialCondition.airspeedKts = properties_.CalibratedAirspeed().Kts();
  initialCondition.pRadPerSec = properties_.Get(CurrentPRadPerSec);
  initialCondition.qRadPerSec = properties_.Get(CurrentQRadPerSec);
  initialCondition.rRadPerSec = properties_.Get(CurrentRRadPerSec);

  return initialCondition;
}

bool Aircraft::Reset(const SimulationConfig &config,
    const InitialCondition &initialCondition) {
  config_ = config;
  ConfigureSimulation(config);
  controls_.Reset();

  if (!ApplyInitialCondition(initialCondition)) {
    return false;
  }

  ResetSimulationTime();

  controls_.Reset();
  return true;
}

void Aircraft::ResetSimulationTime() { fdm_->Setsim_time(0.0); }

bool Aircraft::InitializeForTrim(const gnc::TrimRequest &request) {
  if (request.mode != gnc::TrimMode::Ground) {
    auto initialCondition = fdm_->GetIC();
    initialCondition->SetVcalibratedKtsIC(request.airspeedKts);
    initialCondition->SetAltitudeASLFtIC(request.altitudeFt);
    initialCondition->SetFlightPathAngleDegIC(request.flightPathAngleDeg);
  }

  PrepareExternalOutputForReset();
  return fdm_->RunIC();
}

void Aircraft::RunTrim(gnc::TrimMode mode) {
  DisableExternalOutput();
  fdm_->DoTrim(ToJSBTrimMode(mode));
  DisableExternalOutput();
}

jsbsim::ControlSystem &Aircraft::GetControls() { return controls_; }

const jsbsim::ControlSystem &Aircraft::GetControls() const { return controls_; }

jsbsim::EngineSystem &Aircraft::GetEngines() { return engines_; }

const jsbsim::EngineSystem &Aircraft::GetEngines() const { return engines_; }

jsbsim::Properties &Aircraft::GetProperties() { return properties_; }

const jsbsim::Properties &Aircraft::GetProperties() const {
  return properties_;
}

void Aircraft::ConfigurePaths() {
  DisableExternalOutput();
  fdm_->SetRootDir(SGPath(ResolveJSBSimRootPath()));
  fdm_->SetAircraftPath(SGPath("aircraft"));
  fdm_->SetEnginePath(SGPath("engine"));
  fdm_->SetSystemsPath(SGPath("systems"));
  ConfigureOutputPath();
}

void Aircraft::ConfigureOutputPath() {
  if (!outputDirectory_.empty()) {
    PrepareExternalOutputForReset();
  }
  RemoveOutputDirectory();

  std::error_code error;
  const std::filesystem::path outputRoot =
      std::filesystem::temp_directory_path(error) / "jsb-flight-console-jsbsim";
  if (error) {
    return;
  }
  std::filesystem::create_directories(outputRoot, error);
  if (error) {
    return;
  }

  const auto timestamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto instanceId = reinterpret_cast<std::uintptr_t>(this);
  for (int attempt = 0; attempt < 16; ++attempt) {
    const std::filesystem::path candidate =
        outputRoot
        / ("instance-" + std::to_string(instanceId) + "-"
            + std::to_string(timestamp) + "-" + std::to_string(attempt));
    error.clear();
    if (std::filesystem::create_directory(candidate, error)) {
      outputDirectory_ = candidate;
      fdm_->SetOutputPath(SGPath(outputDirectory_.generic_string()));
      return;
    }
  }
}

void Aircraft::RemoveOutputDirectory() {
  if (outputDirectory_.empty()) {
    return;
  }

  std::error_code error;
  std::filesystem::remove_all(outputDirectory_, error);
  outputDirectory_.clear();
}

bool Aircraft::LoadAircraft(const SimulationConfig &config) {
  if (!fdm_->LoadModel(config.aircraftName)) {
    std::cerr << "Failed to load " << config.aircraftName << '\n';
    return false;
  }

  DisableExternalOutput();
  std::cout << config.aircraftName << " loaded\n";
  return true;
}

void Aircraft::ConfigureSimulation(const SimulationConfig &config) {
  fdm_->Setdt(config.GetDT());
}

void Aircraft::DisableExternalOutput() { fdm_->DisableOutput(); }

void Aircraft::PrepareExternalOutputForReset() {
  fdm_->GetOutput()->SetStartNewOutput();
  DisableExternalOutput();
}

bool Aircraft::InitializeState() {
  PrepareExternalOutputForReset();
  if (!fdm_->RunIC()) {
    std::cerr << "Failed to initialize simulation\n";
    return false;
  }

  std::cout << "Initial altitude: " << properties_.AltitudeAgl().Ft()
            << " ft\n";
  return true;
}

} // namespace sim
