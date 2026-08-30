#include "integration/flightgear/FlightGearSystem.hpp"

#include "sim/runtime/SimulationContracts.hpp"

#include <iostream>
#include <memory>
#include <utility>

namespace flightgear {
bool FlightGearSystem::Initialize() {
  if (sender_ != nullptr) {
    return true;
  }

  auto sender = std::make_unique<FlightGearSender>();
  if (!sender->IsOpen()) {
    std::cerr << "Failed to initialize FlightGear sender.\n";
    return false;
  }

  sender_ = std::move(sender);
  return true;
}

void FlightGearSystem::Update(const sim::SimulationInstanceSnapshot &snapshot) {
  if (sender_ != nullptr && snapshot.available && !sender_->Send(snapshot)) {
    std::cerr << "Failed to send FlightGear packet\n";
  }
}

void FlightGearSystem::Shutdown() { sender_.reset(); }
} // namespace flightgear
