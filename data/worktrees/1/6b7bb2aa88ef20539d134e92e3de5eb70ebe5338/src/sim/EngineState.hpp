#pragma once

#include <cstddef>

namespace sim {
struct EngineState {
  std::size_t index = 0;
  bool running = false;
  double rpm = 0.0;
  double throttleCommand = 0.0;
};
} // namespace sim
