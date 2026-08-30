#pragma once

#include "sim/linearization/LinearizationPerturbation.hpp"

#include <Eigen/Core>
#include <array>

namespace sim {
class Aircraft;
struct FDMState;

using StateVector = Eigen::Matrix<double, 12, 1>;
using StateDerivativeVector = Eigen::Matrix<double, 12, 1>;

using InputVector = Eigen::Matrix<double, 4, 1>;

using StateMatrix = Eigen::Matrix<double, 12, 12>;
using InputMatrix = Eigen::Matrix<double, 12, 4>;

// LinearizationState defines the fixed state-vector ordering in native FDM
// units:
// U, V, W (ft/s); P, Q, R (rad/s); Roll, Pitch, Heading (rad);
// Latitude, Longitude (rad); Altitude (ft).
// StateDerivativeVector reuses the same indices for UDot, VDot, WDot (ft/s^2),
// PDot, QDot, RDot (rad/s^2), Euler rates (rad/s), geographic rates
// (rad/s), and AltitudeDot (ft/s).
// LinearizationInput defines the fixed input-vector ordering. All values are
// normalized commands.

static_assert(
    ToIndex(LinearizationState::Count) == StateVector::RowsAtCompileTime);
static_assert(ToIndex(LinearizationState::Count)
              == StateDerivativeVector::RowsAtCompileTime);
static_assert(
    ToIndex(LinearizationState::Count) == StateMatrix::RowsAtCompileTime);
static_assert(
    ToIndex(LinearizationState::Count) == StateMatrix::ColsAtCompileTime);
static_assert(
    ToIndex(LinearizationState::Count) == InputMatrix::RowsAtCompileTime);
static_assert(
    ToIndex(LinearizationInput::Count) == InputVector::RowsAtCompileTime);
static_assert(
    ToIndex(LinearizationInput::Count) == InputMatrix::ColsAtCompileTime);

StateVector ExtractStateVector(const FDMState &state);
void ApplyStateVector(FDMState &state, const StateVector &vector);

InputVector ExtractInputVector(const FDMState &state);
bool ApplyInputVector(FDMState &state, const InputVector &vector);

StateDerivativeVector ExtractStateDerivativeVector(const Aircraft &aircraft);

constexpr std::array<double, ToIndex(LinearizationState::Count)> StateSteps = {
    /* U */ 0.5,
    /* V */ 0.5,
    /* W */ 0.5,
    /* P */ 1e-3,
    /* Q */ 1e-3,
    /* R */ 1e-3,
    /* Roll */ 1e-3,
    /* Pitch */ 1e-3,
    /* Heading */ 1e-3,
    /* Latitude */ 1e-6,
    /* Longitude */ 1e-6,
    /* Altitude */ 1.0,
};
constexpr std::array<double, ToIndex(LinearizationInput::Count)> InputSteps = {
    1e-3, // aileron
    1e-3, // elevator
    1e-3, // rudder
    1e-3, // throttle
};
} // namespace sim
