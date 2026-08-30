#pragma once

#include "sim/control/ControlInput.hpp"

namespace sim {
class Aircraft;
struct Tick;
} // namespace sim

namespace control {
class IFlightControlSource {
public:
  virtual ~IFlightControlSource() = default;

  virtual ControlInput OnTick(sim::Aircraft &aircraft,
      const sim::Tick &tick) = 0;
};
} // namespace control
