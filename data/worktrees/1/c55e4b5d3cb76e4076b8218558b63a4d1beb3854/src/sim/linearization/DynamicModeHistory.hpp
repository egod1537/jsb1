#pragma once

#include "sim/linearization/DynamicModeContracts.hpp"

#include <vector>

namespace gnc {
class DynamicModeHistory {
public:
  // Snapshot lifecycle
  void Push(DynamicModeSnapshot snapshot);
  void Clear();

  // Time-based lookup
  const DynamicModeSnapshot *FindLatestAtOrBefore(double timeSec) const;
  const std::vector<DynamicModeSnapshot> &GetSnapshots() const;

private:
  std::vector<DynamicModeSnapshot> snapshots_;
};
} // namespace gnc
