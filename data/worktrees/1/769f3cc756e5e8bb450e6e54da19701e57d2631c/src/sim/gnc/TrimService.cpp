#include "sim/gnc/TrimService.hpp"

#include "sim/Aircraft.hpp"
#include "sim/gnc/TrimSolver.hpp"

namespace gnc {
bool TrimService::Compute(sim::Aircraft &aircraft, const TrimRequest &request) {
  return StoreSolvedResult(TrimSolver::Solve(aircraft, request));
}

bool TrimService::ComputeCurrentState(sim::Aircraft &aircraft, TrimMode mode) {
  return StoreSolvedResult(TrimSolver::SolveCurrentState(aircraft, mode));
}

bool TrimService::ApplyStored(sim::Aircraft &aircraft) const {
  if (!result_ || !result_->success) {
    return false;
  }

  aircraft.GetControls().SetInput({
      .elevator = result_->elevator,
      .aileron = result_->aileron,
      .rudder = result_->rudder,
      .throttle = result_->throttle,
  });
  aircraft.GetControls().SetPitchTrim(result_->pitchTrim);
  return true;
}

void TrimService::Clear() { result_.reset(); }

bool TrimService::HasResult() const { return result_.has_value(); }

const TrimResult *TrimService::GetResult() const {
  return result_ ? &*result_ : nullptr;
}

bool TrimService::StoreSolvedResult(const TrimResult &result) {
  if (!result.success) {
    return false;
  }
  result_ = result;
  return true;
}
} // namespace gnc
