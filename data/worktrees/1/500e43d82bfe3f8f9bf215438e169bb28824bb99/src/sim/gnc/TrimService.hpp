#pragma once

#include "sim/gnc/TrimTypes.hpp"

#include <optional>

namespace sim {
class Aircraft;
} // namespace sim

namespace gnc {
class TrimService {
public:
  // Trim calculation
  bool Compute(sim::Aircraft &aircraft, const TrimRequest &request);
  bool ComputeCurrentState(sim::Aircraft &aircraft,
      TrimMode mode = TrimMode::Longitudinal);

  // Stored result
  bool ApplyStored(sim::Aircraft &aircraft) const;
  void Clear();
  bool HasResult() const;
  const TrimResult *GetResult() const;

private:
  bool StoreSolvedResult(const TrimResult &result);

  std::optional<TrimResult> result_;
};
} // namespace gnc
