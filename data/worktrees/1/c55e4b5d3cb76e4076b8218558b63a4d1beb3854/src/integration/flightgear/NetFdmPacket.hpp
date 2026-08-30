#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace flightgear {
constexpr std::uint32_t NetFdmVersion = 24;

constexpr std::size_t MaxEngines = 4;
constexpr std::size_t MaxTanks = 4;
constexpr std::size_t MaxWheels = 3;

struct NetFdmPacket {
  std::uint32_t version = NetFdmVersion;
  std::uint32_t padding = 0;

  double longitude = 0.0;
  double latitude = 0.0;
  double altitude = 0.0;

  float agl = 0.0f;
  float phi = 0.0f;
  float theta = 0.0f;
  float psi = 0.0f;
  float alpha = 0.0f;
  float beta = 0.0f;

  float phidot = 0.0f;
  float thetadot = 0.0f;
  float psidot = 0.0f;

  float vcas = 0.0f;
  float climbRate = 0.0f;

  float vN = 0.0f;
  float vE = 0.0f;
  float vD = 0.0f;

  float vBodyU = 0.0f;
  float vBodyV = 0.0f;
  float vBodyW = 0.0f;

  float aXPilot = 0.0f;
  float aYPilot = 0.0f;
  float aZPilot = 0.0f;

  float stallWarning = 0.0f;
  float slipDeg = 0.0f;

  std::uint32_t numEngines = 0;

  std::array<std::uint32_t, MaxEngines> engineState{};
  std::array<float, MaxEngines> rpm{};
  std::array<float, MaxEngines> fuelFlow{};
  std::array<float, MaxEngines> fuelPressure{};
  std::array<float, MaxEngines> egt{};
  std::array<float, MaxEngines> cht{};
  std::array<float, MaxEngines> manifoldPressure{};
  std::array<float, MaxEngines> tit{};
  std::array<float, MaxEngines> oilTemperature{};
  std::array<float, MaxEngines> oilPressure{};

  std::uint32_t numTanks = 0;
  std::array<float, MaxTanks> fuelQuantity{};

  std::uint32_t numWheels = 0;
  std::array<std::uint32_t, MaxWheels> weightOnWheels{};
  std::array<float, MaxWheels> gearPosition{};
  std::array<float, MaxWheels> gearSteer{};
  std::array<float, MaxWheels> gearCompression{};

  std::uint32_t currentTime = 0;
  std::int32_t warp = 0;
  float visibility = 0.0f;

  float elevator = 0.0f;
  float elevatorTrimTab = 0.0f;
  float leftFlap = 0.0f;
  float rightFlap = 0.0f;
  float leftAileron = 0.0f;
  float rightAileron = 0.0f;
  float rudder = 0.0f;
  float noseWheel = 0.0f;
  float speedbrake = 0.0f;
  float spoilers = 0.0f;
};

static_assert(sizeof(float) == 4);
static_assert(sizeof(double) == 8);
static_assert(sizeof(std::uint32_t) == 4);
static_assert(sizeof(NetFdmPacket) == 408);

inline std::uint32_t ByteSwap32(std::uint32_t value) {
  return ((value & 0x000000FFU) << 24) | ((value & 0x0000FF00U) << 8) |
         ((value & 0x00FF0000U) >> 8) | ((value & 0xFF000000U) >> 24);
}

inline std::uint64_t ByteSwap64(std::uint64_t value) {
  return ((value & 0x00000000000000FFULL) << 56) |
         ((value & 0x000000000000FF00ULL) << 40) |
         ((value & 0x0000000000FF0000ULL) << 24) |
         ((value & 0x00000000FF000000ULL) << 8) |
         ((value & 0x000000FF00000000ULL) >> 8) |
         ((value & 0x0000FF0000000000ULL) >> 24) |
         ((value & 0x00FF000000000000ULL) >> 40) |
         ((value & 0xFF00000000000000ULL) >> 56);
}

inline std::uint32_t ToNetwork32(std::uint32_t value) {
  if constexpr (std::endian::native == std::endian::big) {
    return value;
  }

  return ByteSwap32(value);
}

inline float ToNetworkFloat(float value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  const auto networkBits = ToNetwork32(bits);
  return std::bit_cast<float>(networkBits);
}

inline double ToNetworkDouble(double value) {
  if constexpr (std::endian::native == std::endian::big) {
    return value;
  }

  auto bits = std::bit_cast<std::uint64_t>(value);
  bits = ByteSwap64(bits);
  return std::bit_cast<double>(bits);
}

inline std::int32_t ToNetworkInt32(std::int32_t value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  const auto networkBits = ToNetwork32(bits);
  return std::bit_cast<std::int32_t>(networkBits);
}

inline NetFdmPacket ToNetworkOrder(NetFdmPacket packet) {
  packet.version = ToNetwork32(packet.version);
  packet.padding = ToNetwork32(packet.padding);

  packet.longitude = ToNetworkDouble(packet.longitude);
  packet.latitude = ToNetworkDouble(packet.latitude);
  packet.altitude = ToNetworkDouble(packet.altitude);

  packet.agl = ToNetworkFloat(packet.agl);
  packet.phi = ToNetworkFloat(packet.phi);
  packet.theta = ToNetworkFloat(packet.theta);
  packet.psi = ToNetworkFloat(packet.psi);
  packet.alpha = ToNetworkFloat(packet.alpha);
  packet.beta = ToNetworkFloat(packet.beta);

  return packet;
}

} // namespace flightgear
