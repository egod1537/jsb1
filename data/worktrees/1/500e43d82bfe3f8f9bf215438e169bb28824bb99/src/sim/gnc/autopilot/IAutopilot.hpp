#pragma once

#include "sim/control/ControlInput.hpp"

namespace sim {
class Aircraft;
struct Tick;
} // namespace sim

namespace gnc {
class IAutopilot {
public:
  virtual ~IAutopilot() = default;

  virtual void Reset() = 0;
  virtual control::ControlInput Update(sim::Aircraft &aircraft,
      const sim::Tick &tick,
      const control::ControlInput &passthroughCommand) = 0;
};
} // namespace gnc
