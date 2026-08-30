#include "FlightGearSender.hpp"
#include "sim/runtime/SimulationContracts.hpp"
#include "NetFdmPacket.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <memory>

namespace {
flightgear::NetFdmPacket BuildPacket(
    const sim::SimulationInstanceSnapshot &snapshot) {
  flightgear::NetFdmPacket packet{};
  const sim::FDMKinematicState &state = snapshot.fdmState.state;

  packet.longitude = state.longitudeRad;
  packet.latitude = state.latitudeRad;

  packet.altitude = state.altitudeAslFt * 0.3048;
  packet.agl = static_cast<float>(snapshot.aircraft.altitudeAglFt * 0.3048);

  packet.phi = static_cast<float>(state.attitudeRad[0]);
  packet.theta = static_cast<float>(state.attitudeRad[1]);
  packet.psi = static_cast<float>(state.attitudeRad[2]);

  return packet;
}
} // namespace

namespace flightgear {
class FlightGearSender::Impl {
public:
#ifdef _WIN32
  SOCKET SocketFd = INVALID_SOCKET;
  bool WinsockInitialized = false;
#else
  int SocketFd = -1;
#endif
  sockaddr_in Address{};
};

FlightGearSender::FlightGearSender() : m_Impl(std::make_unique<Impl>()) {
#ifdef _WIN32
  WSADATA wsaData{};
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    return;
  }
  m_Impl->WinsockInitialized = true;

  m_Impl->SocketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  if (m_Impl->SocketFd == INVALID_SOCKET) {
    return;
  }
#else
  m_Impl->SocketFd = socket(AF_INET, SOCK_DGRAM, 0);

  if (m_Impl->SocketFd < 0) {
    return;
  }
#endif

  m_Impl->Address.sin_family = AF_INET;
  m_Impl->Address.sin_port = htons(5500);
  inet_pton(AF_INET, "127.0.0.1", &m_Impl->Address.sin_addr);
}

FlightGearSender::~FlightGearSender() {
  if (m_Impl == nullptr) {
    return;
  }

#ifdef _WIN32
  if (m_Impl->SocketFd != INVALID_SOCKET) {
    closesocket(m_Impl->SocketFd);
  }

  if (m_Impl->WinsockInitialized) {
    WSACleanup();
  }
#else
  if (m_Impl->SocketFd >= 0) {
    close(m_Impl->SocketFd);
  }
#endif
}

bool FlightGearSender::IsOpen() const {
  if (m_Impl == nullptr) {
    return false;
  }

#ifdef _WIN32
  return m_Impl->SocketFd != INVALID_SOCKET;
#else
  return m_Impl->SocketFd >= 0;
#endif
}

bool FlightGearSender::Send(const sim::SimulationInstanceSnapshot &snapshot) {
  if (!IsOpen()) {
    return false;
  }

  const auto packet = BuildPacket(snapshot);
  const auto networkPacket = flightgear::ToNetworkOrder(packet);

#ifdef _WIN32
  const int sentBytes =
      sendto(m_Impl->SocketFd, reinterpret_cast<const char *>(&networkPacket),
             static_cast<int>(sizeof(networkPacket)), 0,
             reinterpret_cast<const sockaddr *>(&m_Impl->Address),
             sizeof(m_Impl->Address));

  return sentBytes == static_cast<int>(sizeof(networkPacket));
#else
  const ssize_t sentBytes =
      sendto(m_Impl->SocketFd, &networkPacket, sizeof(networkPacket), 0,
             reinterpret_cast<const sockaddr *>(&m_Impl->Address),
             sizeof(m_Impl->Address));

  return sentBytes == static_cast<ssize_t>(sizeof(networkPacket));
#endif
}

} // namespace flightgear
//
