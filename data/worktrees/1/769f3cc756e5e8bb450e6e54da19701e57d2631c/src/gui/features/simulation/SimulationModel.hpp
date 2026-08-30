#pragma once

#include "sim/InitialCondition.hpp"

namespace gui {
struct InitialConditionModel {
  sim::InitialCondition pending;
  bool initialized = false;
};
} // namespace gui
