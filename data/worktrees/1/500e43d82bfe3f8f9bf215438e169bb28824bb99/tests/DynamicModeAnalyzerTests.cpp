#include "sim/linearization/DynamicModeAnalyzer.hpp"
#include "sim/linearization/DynamicModeHistory.hpp"
#include "sim/linearization/LinearizationResult.hpp"

#include <Eigen/Dense>

#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr double Tolerance = 1.0e-9;

void Require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void RequireNear(double actual, double expected, const std::string &message) {
  if (std::abs(actual - expected) > Tolerance) {
    throw std::runtime_error(message);
  }
}

gnc::LinearizationResult MakeLinearization(Eigen::MatrixXd A,
    std::vector<std::string> stateNames) {
  gnc::LinearizationResult result;
  result.A = std::move(A);
  result.stateNames = std::move(stateNames);
  return result;
}

const gnc::DynamicMode &FindMode(const gnc::DynamicModeAnalysis &analysis,
    double real, double imaginaryMagnitude) {
  for (const gnc::DynamicMode &mode : analysis.modes) {
    if (std::abs(mode.eigenvalue.real() - real) <= Tolerance
        && std::abs(std::abs(mode.eigenvalue.imag()) - imaginaryMagnitude)
               <= Tolerance) {
      return mode;
    }
  }
  throw std::runtime_error("Expected dynamic mode was not found");
}

void TestStableRealPole() {
  Eigen::MatrixXd A(1, 1);
  A << -2.0;
  const gnc::DynamicModeAnalysis analysis =
      gnc::DynamicModeAnalyzer::Analyze(MakeLinearization(std::move(A), {"P"}),
          12.5);

  Require(analysis.valid, "Stable real-pole analysis was invalid");
  Require(analysis.modes.size() == 1, "Stable real pole count was incorrect");
  const gnc::DynamicMode &mode = analysis.modes.front();
  Require(mode.stability == gnc::DynamicModeStability::Stable,
      "Negative real pole was not stable");
  Require(mode.classification == gnc::DynamicModeClassification::Roll,
      "P-dominated fast real pole was not classified as Roll");
  RequireNear(mode.eigenvalue.real(), -2.0, "Stable pole value was incorrect");
  RequireNear(mode.naturalFrequencyRadPerSec,
      2.0,
      "Stable pole natural frequency was incorrect");
  Require(mode.dampingRatio.has_value(),
      "Stable pole damping ratio was undefined");
  RequireNear(*mode.dampingRatio,
      1.0,
      "Stable pole damping ratio was incorrect");
  Require(!mode.periodSec.has_value(), "Real pole unexpectedly had a period");
  RequireNear(analysis.linearizationSimTimeSec,
      12.5,
      "Linearization timestamp was not retained");
}

void TestUnstableRealPole() {
  Eigen::MatrixXd A(1, 1);
  A << 0.5;
  const gnc::DynamicModeAnalysis analysis = gnc::DynamicModeAnalyzer::Analyze(
      MakeLinearization(std::move(A), {"Phi"}),
      0.0);

  Require(analysis.valid, "Unstable real-pole analysis was invalid");
  const gnc::DynamicMode &mode = analysis.modes.front();
  Require(mode.stability == gnc::DynamicModeStability::Unstable,
      "Positive real pole was not unstable");
  Require(mode.dampingRatio.has_value(),
      "Unstable pole damping ratio was undefined");
  RequireNear(*mode.dampingRatio,
      -1.0,
      "Unstable pole damping ratio was incorrect");
}

void TestStableComplexPairMetricsAndDeduplication() {
  constexpr double Sigma = -0.2;
  constexpr double OmegaD = 1.5;
  Eigen::MatrixXd A(2, 2);
  A << Sigma, -OmegaD, OmegaD, Sigma;
  const gnc::DynamicModeAnalysis analysis = gnc::DynamicModeAnalyzer::Analyze(
      MakeLinearization(std::move(A), {"Beta", "R"}),
      0.0);

  Require(analysis.valid, "Complex-pair analysis was invalid");
  Require(analysis.modes.size() == 1,
      "Conjugate eigenvalues were not deduplicated");
  const gnc::DynamicMode &mode = analysis.modes.front();
  const double expectedNaturalFrequency = std::hypot(Sigma, OmegaD);
  RequireNear(mode.eigenvalue.real(), Sigma, "Complex sigma was incorrect");
  RequireNear(mode.eigenvalue.imag(),
      OmegaD,
      "Complex damped frequency was incorrect");
  RequireNear(mode.naturalFrequencyRadPerSec,
      expectedNaturalFrequency,
      "Complex natural frequency was incorrect");
  Require(mode.dampingRatio.has_value(), "Complex damping ratio was undefined");
  RequireNear(*mode.dampingRatio,
      -Sigma / expectedNaturalFrequency,
      "Complex damping ratio was incorrect");
  Require(mode.periodSec.has_value(), "Complex mode period was undefined");
  RequireNear(*mode.periodSec,
      2.0 * std::numbers::pi / OmegaD,
      "Complex mode period was incorrect");
  Require(mode.stability == gnc::DynamicModeStability::Stable,
      "Stable complex pair was not stable");
  Require(mode.classification == gnc::DynamicModeClassification::DutchRoll,
      "Beta/R-dominated complex pair was not classified as Dutch Roll");
}

void TestDominantStateNormalization() {
  Eigen::MatrixXd A(2, 2);
  A << -1.0, 1.0, 0.0, -3.0;
  const gnc::DynamicModeAnalysis analysis = gnc::DynamicModeAnalyzer::Analyze(
      MakeLinearization(std::move(A), {"First", "Second"}),
      0.0);

  const gnc::DynamicMode &mode = FindMode(analysis, -3.0, 0.0);
  Require(mode.stateParticipations.size() == 2,
      "State participation count was incorrect");
  Require(mode.stateParticipations[0].stateName == "Second",
      "Dominant state was not sorted first");
  RequireNear(mode.stateParticipations[0].normalizedMagnitude,
      1.0,
      "Dominant state was not normalized to one");
  Require(mode.stateParticipations[1].stateName == "First",
      "Secondary state ordering was incorrect");
  RequireNear(mode.stateParticipations[1].normalizedMagnitude,
      0.5,
      "Secondary state normalization was incorrect");
}

void TestNearZeroModeIsSafe() {
  Eigen::MatrixXd A = Eigen::MatrixXd::Zero(1, 1);
  const gnc::DynamicModeAnalysis analysis = gnc::DynamicModeAnalyzer::Analyze(
      MakeLinearization(std::move(A), {"State"}),
      0.0);

  Require(analysis.valid, "Zero-mode analysis was invalid");
  const gnc::DynamicMode &mode = analysis.modes.front();
  RequireNear(mode.naturalFrequencyRadPerSec,
      0.0,
      "Zero mode natural frequency was incorrect");
  Require(!mode.dampingRatio.has_value(),
      "Zero mode damping ratio should be undefined");
  Require(!mode.periodSec.has_value(),
      "Zero real mode unexpectedly had a period");
  Require(mode.stability == gnc::DynamicModeStability::Neutral,
      "Zero mode was not neutral");
}

gnc::DynamicModeSnapshot MakeSnapshot(double timeSec, double eigenvalue) {
  gnc::DynamicModeAnalysis analysis;
  analysis.valid = true;
  analysis.modes.push_back({.eigenvalue = {eigenvalue, 0.0}});
  return {
      .simulationTimeSec = timeSec,
      .analysis = std::move(analysis),
  };
}

void TestHistorySelectsLatestSnapshotWithoutUsingFutureData() {
  gnc::DynamicModeHistory history;
  history.Push(MakeSnapshot(100.0, -1.0));
  history.Push(MakeSnapshot(105.0, -2.0));
  history.Push(MakeSnapshot(110.0, -3.0));

  Require(history.FindLatestAtOrBefore(99.9) == nullptr,
      "History selected a future snapshot");
  const gnc::DynamicModeSnapshot *atBoundary =
      history.FindLatestAtOrBefore(105.0);
  Require(atBoundary != nullptr && atBoundary->simulationTimeSec == 105.0,
      "History did not select the snapshot at an exact boundary");
  const gnc::DynamicModeSnapshot *betweenSnapshots =
      history.FindLatestAtOrBefore(107.4);
  Require(betweenSnapshots != nullptr
              && betweenSnapshots->simulationTimeSec == 105.0,
      "History did not select the latest preceding snapshot");
  const gnc::DynamicModeSnapshot *afterHistory =
      history.FindLatestAtOrBefore(500.0);
  Require(afterHistory != nullptr && afterHistory->simulationTimeSec == 110.0,
      "History did not retain the latest snapshot after its timestamp");
}

void TestHistoryOrdersReplacesAndClearsSnapshots() {
  gnc::DynamicModeHistory history;
  history.Push(MakeSnapshot(10.0, -1.0));
  history.Push(MakeSnapshot(5.0, -2.0));
  history.Push(MakeSnapshot(10.0, -3.0));

  const auto &snapshots = history.GetSnapshots();
  Require(snapshots.size() == 2,
      "History did not replace a duplicate timestamp");
  Require(snapshots[0].simulationTimeSec == 5.0
              && snapshots[1].simulationTimeSec == 10.0,
      "History snapshots were not time ordered");
  RequireNear(snapshots[1].analysis.modes.front().eigenvalue.real(),
      -3.0,
      "History did not retain the replacement snapshot");
  RequireNear(snapshots[1].analysis.linearizationSimTimeSec,
      10.0,
      "History did not synchronize the analysis timestamp");

  history.Clear();
  Require(history.GetSnapshots().empty(), "History clear retained snapshots");
  Require(history.FindLatestAtOrBefore(10.0) == nullptr,
      "Cleared history still returned a snapshot");
}
} // namespace

int main() {
  try {
    TestStableRealPole();
    TestUnstableRealPole();
    TestStableComplexPairMetricsAndDeduplication();
    TestDominantStateNormalization();
    TestNearZeroModeIsSafe();
    TestHistorySelectsLatestSnapshotWithoutUsingFutureData();
    TestHistoryOrdersReplacesAndClearsSnapshots();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
