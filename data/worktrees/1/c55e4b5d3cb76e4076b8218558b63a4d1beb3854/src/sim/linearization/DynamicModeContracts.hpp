#pragma once

#include <cstddef>
#include <complex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gnc {
enum class DynamicModeClassification {
  Unknown,
  ShortPeriod,
  Phugoid,
  Roll,
  DutchRoll,
  Spiral,
};

enum class DynamicModeStability {
  Stable,
  Neutral,
  Unstable,
};

struct DynamicModeStateParticipation {
  std::string stateName;
  std::size_t stateIndex = 0;
  double normalizedMagnitude = 0.0;
};

struct DynamicMode {
  std::complex<double> eigenvalue;
  double naturalFrequencyRadPerSec = 0.0;
  std::optional<double> dampingRatio;
  std::optional<double> periodSec;
  DynamicModeStability stability = DynamicModeStability::Neutral;
  DynamicModeClassification classification = DynamicModeClassification::Unknown;
  std::vector<DynamicModeStateParticipation> stateParticipations;
};

struct DynamicModeAnalysis {
  bool valid = false;
  double linearizationSimTimeSec = 0.0;
  std::vector<DynamicMode> modes;
  std::string errorMessage;
};

struct DynamicModeSnapshot {
  double simulationTimeSec = 0.0;
  DynamicModeAnalysis analysis;
};

std::string_view ToString(DynamicModeClassification classification);
std::string_view ToString(DynamicModeStability stability);
} // namespace gnc
