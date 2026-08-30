#include "sim/jsbsim/FDMStateAccess.hpp"

#include <FGFDMExec.h>
#include <algorithm>
#include <math/FGQuaternion.h>
#include <models/FGAtmosphere.h>
#include <models/FGFCS.h>
#include <models/FGInertial.h>
#include <models/FGPropagate.h>
#include <models/FGPropulsion.h>
#include <models/atmosphere/FGStandardAtmosphere.h>
#include <models/atmosphere/FGWinds.h>
#include <models/propulsion/FGEngine.h>
#include <models/propulsion/FGThruster.h>

namespace {
constexpr int FirstAxis = 1;
constexpr int SecondAxis = 2;
constexpr int ThirdAxis = 3;

void ExtractKinematicState(const JSBSim::FGFDMExec &fdmExec,
    sim::FDMKinematicState &state) {
  const auto propagate = fdmExec.GetPropagate();
  if (propagate == nullptr) {
    return;
  }

  state.latitudeRad = propagate->GetLatitude();
  state.longitudeRad = propagate->GetLongitude();
  state.altitudeAslFt = propagate->GetAltitudeASL();
  state.bodyVelocityFps = {
      propagate->GetUVW(FirstAxis),
      propagate->GetUVW(SecondAxis),
      propagate->GetUVW(ThirdAxis),
  };
  state.attitudeRad = {
      propagate->GetEuler(FirstAxis),
      propagate->GetEuler(SecondAxis),
      propagate->GetEuler(ThirdAxis),
  };
  state.bodyAngularRatesRadPerSec = {
      propagate->GetPQR(FirstAxis),
      propagate->GetPQR(SecondAxis),
      propagate->GetPQR(ThirdAxis),
  };
}

void ApplyKinematicState(JSBSim::FGFDMExec &fdmExec,
    const sim::FDMKinematicState &state) {
  const auto propagate = fdmExec.GetPropagate();
  if (propagate == nullptr) {
    return;
  }

  propagate->SetLatitude(state.latitudeRad);
  propagate->SetLongitude(state.longitudeRad);
  propagate->SetAltitudeASL(state.altitudeAslFt);

  const JSBSim::FGQuaternion localAttitude(state.attitudeRad[0],
      state.attitudeRad[1],
      state.attitudeRad[2]);
  const JSBSim::FGQuaternion inertialAttitude =
      propagate->GetTi2l().GetQuaternion() * localAttitude;
  propagate->SetInertialOrientation(inertialAttitude);

  for (int axis = FirstAxis; axis <= ThirdAxis; ++axis) {
    const auto index = static_cast<std::size_t>(axis - FirstAxis);
    propagate->SetUVW(axis, state.bodyVelocityFps[index]);
    propagate->SetPQR(axis, state.bodyAngularRatesRadPerSec[index]);
  }
}

void ExtractControlState(const JSBSim::FGFDMExec &fdmExec,
    sim::FDMControlState &state) {
  const auto fcs = fdmExec.GetFCS();
  if (fcs == nullptr) {
    return;
  }

  state.elevatorCommand = fcs->GetDeCmd();
  state.aileronCommand = fcs->GetDaCmd();
  state.rudderCommand = fcs->GetDrCmd();
  state.throttleCommands = fcs->GetThrottleCmd();
  state.pitchTrimCommand = fcs->GetPitchTrimCmd();

  state.elevatorPositionRad = fcs->GetDePos(JSBSim::ofRad);
  state.leftAileronPositionRad = fcs->GetDaLPos(JSBSim::ofRad);
  state.rightAileronPositionRad = fcs->GetDaRPos(JSBSim::ofRad);
  state.rudderPositionRad = fcs->GetDrPos(JSBSim::ofRad);
  state.throttlePositions = fcs->GetThrottlePos();
}

void ApplyControlState(JSBSim::FGFDMExec &fdmExec,
    const sim::FDMControlState &state) {
  const auto fcs = fdmExec.GetFCS();
  if (fcs == nullptr) {
    return;
  }

  fcs->SetDeCmd(state.elevatorCommand);
  fcs->SetDaCmd(state.aileronCommand);
  fcs->SetDrCmd(state.rudderCommand);
  fcs->SetPitchTrimCmd(state.pitchTrimCommand);

  const std::size_t throttleCommandCount =
      std::min(state.throttleCommands.size(), fcs->GetThrottleCmd().size());
  for (std::size_t index = 0; index < throttleCommandCount; ++index) {
    fcs->SetThrottleCmd(static_cast<int>(index), state.throttleCommands[index]);
  }

  fcs->SetDePos(JSBSim::ofRad, state.elevatorPositionRad);
  fcs->SetDaLPos(JSBSim::ofRad, state.leftAileronPositionRad);
  fcs->SetDaRPos(JSBSim::ofRad, state.rightAileronPositionRad);
  fcs->SetDrPos(JSBSim::ofRad, state.rudderPositionRad);

  const std::size_t throttlePositionCount =
      std::min(state.throttlePositions.size(), fcs->GetThrottlePos().size());
  for (std::size_t index = 0; index < throttlePositionCount; ++index) {
    fcs->SetThrottlePos(static_cast<int>(index),
        state.throttlePositions[index]);
  }
}

void ExtractPropulsionState(const JSBSim::FGFDMExec &fdmExec,
    sim::FDMPropulsionState &state) {
  const auto propulsion = fdmExec.GetPropulsion();
  if (propulsion == nullptr) {
    return;
  }

  state.engines.reserve(propulsion->GetNumEngines());
  for (unsigned int index = 0; index < propulsion->GetNumEngines(); ++index) {
    const auto engine = propulsion->GetEngine(index);
    if (engine == nullptr) {
      state.engines.emplace_back();
      continue;
    }

    sim::FDMEngineState engineState{};
    engineState.running = engine->GetRunning();
    const auto thruster = engine->GetThruster();
    if (thruster != nullptr) {
      engineState.engineRpm = thruster->GetEngineRPM();
      engineState.thrusterRpm = thruster->GetRPM();
    }
    state.engines.push_back(engineState);
  }
}

void ApplyPropulsionState(JSBSim::FGFDMExec &fdmExec,
    const sim::FDMPropulsionState &state) {
  const auto propulsion = fdmExec.GetPropulsion();
  if (propulsion == nullptr) {
    return;
  }

  const std::size_t engineCount =
      std::min(state.engines.size(), propulsion->GetNumEngines());
  for (std::size_t index = 0; index < engineCount; ++index) {
    const auto engine = propulsion->GetEngine(static_cast<unsigned int>(index));
    if (engine == nullptr) {
      continue;
    }

    const sim::FDMEngineState &engineState = state.engines[index];
    engine->SetRunning(engineState.running);
    const auto thruster = engine->GetThruster();
    if (thruster != nullptr) {
      thruster->SetRPM(engineState.thrusterRpm);
      thruster->SetEngineRPM(engineState.engineRpm);
    }
  }
}

void ExtractEnvironmentState(const JSBSim::FGFDMExec &fdmExec,
    sim::FDMEnvironmentState &state) {
  const auto atmosphere = fdmExec.GetAtmosphere();
  if (atmosphere != nullptr) {
    state.seaLevelTemperatureRankine = atmosphere->GetTemperatureSL();
    state.seaLevelPressurePsf =
        atmosphere->GetPressureSL(JSBSim::FGAtmosphere::ePSF);

    const auto standardAtmosphere =
        std::dynamic_pointer_cast<JSBSim::FGStandardAtmosphere>(atmosphere);
    if (standardAtmosphere != nullptr) {
      state.hasStandardAtmosphere = true;
      state.temperatureBiasRankine = standardAtmosphere->GetTemperatureBias(
          JSBSim::FGAtmosphere::eRankine);
      state.seaLevelGradedTemperatureDeltaRankine =
          standardAtmosphere->GetTemperature(0.0)
          - standardAtmosphere->GetStdTemperature(0.0)
          - state.temperatureBiasRankine;
      state.vaporMassFractionPpm =
          standardAtmosphere->GetVaporMassFractionPPM();
    }
  }

  const auto winds = fdmExec.GetWinds();
  if (winds != nullptr) {
    for (int axis = FirstAxis; axis <= ThirdAxis; ++axis) {
      const auto index = static_cast<std::size_t>(axis - FirstAxis);
      state.windNedFps[index] = winds->GetWindNED(axis);
      state.gustNedFps[index] = winds->GetGustNED(axis);
      state.turbulenceNedFps[index] = winds->GetTurbNED(axis);
    }
    state.turbulenceType = static_cast<int>(winds->GetTurbType());
    state.turbulenceGain = winds->GetTurbGain();
    state.turbulenceRate = winds->GetTurbRate();
    state.turbulenceRhythmicity = winds->GetRhythmicity();
    state.windSpeedAt20FtFps = winds->GetWindspeed20ft();
  }

  const auto propagate = fdmExec.GetPropagate();
  if (propagate != nullptr) {
    state.terrainElevationFt = propagate->GetTerrainElevation();
  }

  const auto inertial = fdmExec.GetInertial();
  if (inertial != nullptr) {
    state.gravityType = inertial->GetGravityType();
    state.planetRotationRateRadPerSec = inertial->GetOmegaPlanet()(ThirdAxis);
  }
}

void ApplyEnvironmentState(JSBSim::FGFDMExec &fdmExec,
    const sim::FDMEnvironmentState &state) {
  const auto atmosphere = fdmExec.GetAtmosphere();
  if (atmosphere != nullptr) {
    const auto standardAtmosphere =
        std::dynamic_pointer_cast<JSBSim::FGStandardAtmosphere>(atmosphere);
    if (state.hasStandardAtmosphere && standardAtmosphere != nullptr) {
      standardAtmosphere->ResetSLTemperature();
      standardAtmosphere->SetTemperatureBias(JSBSim::FGAtmosphere::eRankine,
          state.temperatureBiasRankine);
      standardAtmosphere->SetSLTemperatureGradedDelta(
          JSBSim::FGAtmosphere::eRankine,
          state.seaLevelGradedTemperatureDeltaRankine);
      standardAtmosphere->SetVaporMassFractionPPM(state.vaporMassFractionPpm);
    } else {
      atmosphere->SetTemperatureSL(state.seaLevelTemperatureRankine,
          JSBSim::FGAtmosphere::eRankine);
    }
    atmosphere->SetPressureSL(JSBSim::FGAtmosphere::ePSF,
        state.seaLevelPressurePsf);
  }

  const auto winds = fdmExec.GetWinds();
  if (winds != nullptr) {
    winds->SetWindNED(state.windNedFps[0],
        state.windNedFps[1],
        state.windNedFps[2]);
    winds->SetGustNED(state.gustNedFps[0],
        state.gustNedFps[1],
        state.gustNedFps[2]);
    for (int axis = FirstAxis; axis <= ThirdAxis; ++axis) {
      const auto index = static_cast<std::size_t>(axis - FirstAxis);
      winds->SetTurbNED(axis, state.turbulenceNedFps[index]);
    }
    winds->SetTurbType(
        static_cast<JSBSim::FGWinds::tType>(state.turbulenceType));
    winds->SetTurbGain(state.turbulenceGain);
    winds->SetTurbRate(state.turbulenceRate);
    winds->SetRhythmicity(state.turbulenceRhythmicity);
    winds->SetWindspeed20ft(state.windSpeedAt20FtFps);
  }

  const auto propagate = fdmExec.GetPropagate();
  if (propagate != nullptr) {
    propagate->SetTerrainElevation(state.terrainElevationFt);
  }

  const auto inertial = fdmExec.GetInertial();
  if (inertial != nullptr) {
    inertial->SetGravityType(state.gravityType);
    inertial->SetOmegaPlanet(state.planetRotationRateRadPerSec);
  }
}
} // namespace

namespace sim::jsbsim {
FDMStateAccess::FDMStateAccess(JSBSim::FGFDMExec &fdmExec)
    : fdmExec_(fdmExec) {}

FDMState FDMStateAccess::Extract(FDMStateFlags flags) const {
  FDMState state{};
  state.flags = flags;

  if (HasFDMStateFlag(flags, FDMStateFlags::State)) {
    ExtractKinematicState(fdmExec_, state.state);
  }
  if (HasFDMStateFlag(flags, FDMStateFlags::Controls)) {
    ExtractControlState(fdmExec_, state.controls);
  }
  if (HasFDMStateFlag(flags, FDMStateFlags::Propulsion)) {
    ExtractPropulsionState(fdmExec_, state.propulsion);
  }
  if (HasFDMStateFlag(flags, FDMStateFlags::Environment)) {
    ExtractEnvironmentState(fdmExec_, state.environment);
  }

  return state;
}

void FDMStateAccess::Apply(const FDMState &state) {
  if (HasFDMStateFlag(state.flags, FDMStateFlags::Environment)) {
    ApplyEnvironmentState(fdmExec_, state.environment);
  }
  if (HasFDMStateFlag(state.flags, FDMStateFlags::State)) {
    ApplyKinematicState(fdmExec_, state.state);
  }
  if (HasFDMStateFlag(state.flags, FDMStateFlags::Controls)) {
    ApplyControlState(fdmExec_, state.controls);
  }
  if (HasFDMStateFlag(state.flags, FDMStateFlags::Propulsion)) {
    ApplyPropulsionState(fdmExec_, state.propulsion);
  }
}
} // namespace sim::jsbsim
