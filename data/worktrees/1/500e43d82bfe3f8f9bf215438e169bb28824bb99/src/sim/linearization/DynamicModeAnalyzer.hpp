#pragma once

#include "sim/linearization/DynamicModeContracts.hpp"

namespace gnc {
struct LinearizationResult;

class DynamicModeAnalyzer {
public:
  static DynamicModeAnalysis Analyze(const LinearizationResult &linearization,
      double linearizationSimTimeSec);
};
} // namespace gnc
