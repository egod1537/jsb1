#pragma once

#include "sim/gnc/TrimTypes.hpp"

namespace sim {
class Aircraft;
}

namespace gnc::TrimSolver {
TrimResult Solve(sim::Aircraft &aircraft, const TrimRequest &request);
TrimResult SolveCurrentState(sim::Aircraft &aircraft,
    TrimMode mode = TrimMode::Longitudinal);
} // namespace gnc::TrimSolver
