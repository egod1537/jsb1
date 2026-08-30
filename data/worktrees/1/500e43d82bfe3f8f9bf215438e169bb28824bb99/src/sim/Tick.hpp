#pragma once

#include <cstdint>

namespace sim {
struct Tick {
  std::uint64_t index = 0;
  double dtSec = 0.0;
  double simTimeSec = 0.0;
};
} // namespace sim
