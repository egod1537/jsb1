#pragma once

#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace sim {
enum class FDMStateFlags : std::uint32_t {
  None = 0,
  State = 1u << 0,
  Controls = 1u << 1,
  Propulsion = 1u << 2,
  Environment = 1u << 3,

  All = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3),
};

constexpr FDMStateFlags operator|(FDMStateFlags left,
    FDMStateFlags right) noexcept {
  using Value = std::underlying_type_t<FDMStateFlags>;
  return static_cast<FDMStateFlags>(
      static_cast<Value>(left) | static_cast<Value>(right));
}

constexpr FDMStateFlags operator&(FDMStateFlags left,
    FDMStateFlags right) noexcept {
  using Value = std::underlying_type_t<FDMStateFlags>;
  return static_cast<FDMStateFlags>(
      static_cast<Value>(left) & static_cast<Value>(right));
}

constexpr FDMStateFlags operator^(FDMStateFlags left,
    FDMStateFlags right) noexcept {
  using Value = std::underlying_type_t<FDMStateFlags>;
  return static_cast<FDMStateFlags>(
      static_cast<Value>(left) ^ static_cast<Value>(right));
}

constexpr FDMStateFlags operator~(FDMStateFlags value) noexcept {
  using Value = std::underlying_type_t<FDMStateFlags>;
  return static_cast<FDMStateFlags>(~static_cast<Value>(value));
}

constexpr FDMStateFlags &operator|=(FDMStateFlags &left,
    FDMStateFlags right) noexcept {
  return left = left | right;
}

constexpr FDMStateFlags &operator&=(FDMStateFlags &left,
    FDMStateFlags right) noexcept {
  return left = left & right;
}

constexpr FDMStateFlags &operator^=(FDMStateFlags &left,
    FDMStateFlags right) noexcept {
  return left = left ^ right;
}

constexpr bool HasFDMStateFlag(FDMStateFlags flags,
    FDMStateFlags flag) noexcept {
  return (flags & flag) == flag;
}

struct FDMKinematicState {
  double latitudeRad = 0.0;
  double longitudeRad = 0.0;
  double altitudeAslFt = 0.0;
  std::array<double, 3> bodyVelocityFps{};
  std::array<double, 3> attitudeRad{};
  std::array<double, 3> bodyAngularRatesRadPerSec{};
};

struct FDMControlState {
  double elevatorCommand = 0.0;
  double aileronCommand = 0.0;
  double rudderCommand = 0.0;
  std::vector<double> throttleCommands;
  double pitchTrimCommand = 0.0;

  double elevatorPositionRad = 0.0;
  double leftAileronPositionRad = 0.0;
  double rightAileronPositionRad = 0.0;
  double rudderPositionRad = 0.0;
  std::vector<double> throttlePositions;
};

struct FDMEngineState {
  bool running = false;
  double engineRpm = 0.0;
  double thrusterRpm = 0.0;
};

struct FDMPropulsionState {
  std::vector<FDMEngineState> engines;
};

struct FDMEnvironmentState {
  double seaLevelTemperatureRankine = 0.0;
  double seaLevelPressurePsf = 0.0;

  bool hasStandardAtmosphere = false;
  double temperatureBiasRankine = 0.0;
  double seaLevelGradedTemperatureDeltaRankine = 0.0;
  double vaporMassFractionPpm = 0.0;

  std::array<double, 3> windNedFps{};
  std::array<double, 3> gustNedFps{};
  std::array<double, 3> turbulenceNedFps{};
  int turbulenceType = 0;
  double turbulenceGain = 0.0;
  double turbulenceRate = 0.0;
  double turbulenceRhythmicity = 0.0;
  double windSpeedAt20FtFps = 0.0;

  double terrainElevationFt = 0.0;
  int gravityType = 0;
  double planetRotationRateRadPerSec = 0.0;
};

struct FDMState {
  FDMStateFlags flags = FDMStateFlags::None;
  FDMKinematicState state;
  FDMControlState controls;
  FDMPropulsionState propulsion;
  FDMEnvironmentState environment;
};
} // namespace sim
