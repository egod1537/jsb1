#include "sim/linearization/DynamicModeHistory.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace gnc {
void DynamicModeHistory::Push(DynamicModeSnapshot snapshot) {
  if (!std::isfinite(snapshot.simulationTimeSec) || !snapshot.analysis.valid) {
    return;
  }

  snapshot.analysis.linearizationSimTimeSec = snapshot.simulationTimeSec;
  const auto position = std::lower_bound(snapshots_.begin(),
      snapshots_.end(),
      snapshot.simulationTimeSec,
      [](const DynamicModeSnapshot &candidate, double timeSec) {
        return candidate.simulationTimeSec < timeSec;
      });
  if (position != snapshots_.end()
      && position->simulationTimeSec == snapshot.simulationTimeSec) {
    *position = std::move(snapshot);
  } else {
    snapshots_.insert(position, std::move(snapshot));
  }
}

void DynamicModeHistory::Clear() { snapshots_.clear(); }

const DynamicModeSnapshot *DynamicModeHistory::FindLatestAtOrBefore(
    double timeSec) const {
  if (!std::isfinite(timeSec) || snapshots_.empty()) {
    return nullptr;
  }

  const auto position = std::upper_bound(snapshots_.begin(),
      snapshots_.end(),
      timeSec,
      [](double selectedTimeSec, const DynamicModeSnapshot &candidate) {
        return selectedTimeSec < candidate.simulationTimeSec;
      });
  return position == snapshots_.begin() ? nullptr : &*std::prev(position);
}

const std::vector<DynamicModeSnapshot> &
DynamicModeHistory::GetSnapshots() const {
  return snapshots_;
}
} // namespace gnc
