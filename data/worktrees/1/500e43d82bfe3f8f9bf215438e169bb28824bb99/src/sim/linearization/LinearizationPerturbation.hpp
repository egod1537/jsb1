#pragma once

#include <Eigen/Core>

namespace sim {
struct FDMState;

enum class LinearizationState : Eigen::Index {
  U = 0,
  V,
  W,
  P,
  Q,
  R,
  Roll,
  Pitch,
  Heading,
  Latitude,
  Longitude,
  Altitude,
  Count,
};

enum class LinearizationInput : Eigen::Index {
  Aileron = 0,
  Elevator,
  Rudder,
  Throttle,
  Count,
};

constexpr Eigen::Index ToIndex(LinearizationState state) {
  return static_cast<Eigen::Index>(state);
}

constexpr Eigen::Index ToIndex(LinearizationInput input) {
  return static_cast<Eigen::Index>(input);
}

struct StatePerturbation {
  LinearizationState variable;
  double amount;
};

struct InputPerturbation {
  LinearizationInput variable;
  double amount;
};

void ApplyPerturbation(FDMState &state, const StatePerturbation &perturbation);
void ApplyPerturbation(FDMState &state, const InputPerturbation &perturbation);
} // namespace sim
