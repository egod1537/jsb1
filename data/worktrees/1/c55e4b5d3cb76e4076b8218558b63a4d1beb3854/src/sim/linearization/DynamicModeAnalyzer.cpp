#include "sim/linearization/DynamicModeAnalyzer.hpp"

#include "sim/linearization/LinearizationResult.hpp"

#include <Eigen/Eigenvalues>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>

namespace {
constexpr double EigenvalueZeroTolerance = 1.0e-8;
constexpr double ConjugatePairRelativeTolerance = 1.0e-6;
constexpr double MinimumNaturalFrequency = 1.0e-10;

std::string NormalizeStateName(std::string_view name) {
  std::string normalized;
  normalized.reserve(name.size());
  for (const char character : name) {
    if (character >= 'A' && character <= 'Z') {
      normalized.push_back(static_cast<char>(character - 'A' + 'a'));
    } else if (character != '_' && character != '-' && character != ' ') {
      normalized.push_back(character);
    }
  }
  return normalized;
}

double GetParticipation(const gnc::DynamicMode &mode,
    std::initializer_list<std::string_view> stateNames) {
  double participation = 0.0;
  for (const gnc::DynamicModeStateParticipation &state :
      mode.stateParticipations) {
    const std::string normalizedName = NormalizeStateName(state.stateName);
    for (const std::string_view candidate : stateNames) {
      if (normalizedName == NormalizeStateName(candidate)) {
        participation = std::max(participation, state.normalizedMagnitude);
      }
    }
  }
  return participation;
}

gnc::DynamicModeClassification ClassifyMode(const gnc::DynamicMode &mode) {
  const double beta = GetParticipation(mode, {"Beta"});
  const double p = GetParticipation(mode, {"P"});
  const double r = GetParticipation(mode, {"R"});
  const double phi = GetParticipation(mode, {"Phi", "Roll"});

  const double alpha = GetParticipation(mode, {"Alpha"});
  const double q = GetParticipation(mode, {"Q"});
  const double theta = GetParticipation(mode, {"Theta", "Pitch"});
  const double forwardSpeed =
      GetParticipation(mode, {"Vt", "Va", "U", "ForwardSpeed"});

  const double lateral = std::max({beta, p, r, phi});
  const double longitudinal = std::max({alpha, q, theta, forwardSpeed});
  const bool isComplex =
      std::abs(mode.eigenvalue.imag()) > EigenvalueZeroTolerance;

  if (isComplex) {
    const double dutchRollCore = std::max(beta, r);
    if (dutchRollCore >= 0.35
        && lateral >= std::max(0.35, longitudinal * 1.10)) {
      return gnc::DynamicModeClassification::DutchRoll;
    }

    const double shortPeriodCore = std::max(alpha, q);
    if (shortPeriodCore >= 0.35 && mode.naturalFrequencyRadPerSec >= 0.50
        && longitudinal >= std::max(0.35, lateral * 1.10)) {
      return gnc::DynamicModeClassification::ShortPeriod;
    }

    const double phugoidCore = std::max(forwardSpeed, theta);
    if (phugoidCore >= 0.25 && mode.naturalFrequencyRadPerSec < 0.50
        && longitudinal >= std::max(0.25, lateral * 1.10)) {
      return gnc::DynamicModeClassification::Phugoid;
    }
    return gnc::DynamicModeClassification::Unknown;
  }

  const double poleSpeed = std::abs(mode.eigenvalue.real());
  if (p >= 0.50 && poleSpeed >= 0.25
      && lateral >= std::max(0.50, longitudinal)) {
    return gnc::DynamicModeClassification::Roll;
  }
  if (phi >= 0.25 && poleSpeed <= 0.25
      && lateral >= std::max(0.25, longitudinal)) {
    return gnc::DynamicModeClassification::Spiral;
  }
  return gnc::DynamicModeClassification::Unknown;
}

bool IsNearReal(const std::complex<double> &eigenvalue) {
  const double scale = std::max(1.0, std::abs(eigenvalue));
  return std::abs(eigenvalue.imag()) <= EigenvalueZeroTolerance * scale;
}

bool IsConjugatePair(const std::complex<double> &first,
    const std::complex<double> &second) {
  const double scale = std::max({1.0, std::abs(first), std::abs(second)});
  return std::abs(second - std::conj(first))
         <= ConjugatePairRelativeTolerance * scale;
}

std::vector<gnc::DynamicModeStateParticipation> CalculateParticipation(
    const Eigen::VectorXcd &eigenvector,
    const std::vector<std::string> &stateNames) {
  double maximumMagnitude = 0.0;
  for (Eigen::Index index = 0; index < eigenvector.size(); ++index) {
    maximumMagnitude = std::max(maximumMagnitude, std::abs(eigenvector(index)));
  }
  if (!std::isfinite(maximumMagnitude)
      || maximumMagnitude <= std::numeric_limits<double>::epsilon()) {
    return {};
  }

  std::vector<gnc::DynamicModeStateParticipation> participation;
  participation.reserve(static_cast<std::size_t>(eigenvector.size()));
  for (Eigen::Index index = 0; index < eigenvector.size(); ++index) {
    const std::size_t stateIndex = static_cast<std::size_t>(index);
    const double normalizedMagnitude =
        std::abs(eigenvector(index)) / maximumMagnitude;
    if (!std::isfinite(normalizedMagnitude)) {
      continue;
    }
    participation.push_back({
        .stateName = stateIndex < stateNames.size()
                         ? stateNames[stateIndex]
                         : "State " + std::to_string(stateIndex),
        .stateIndex = stateIndex,
        .normalizedMagnitude = normalizedMagnitude,
    });
  }

  std::stable_sort(participation.begin(),
      participation.end(),
      [](const auto &left, const auto &right) {
        return left.normalizedMagnitude > right.normalizedMagnitude;
      });
  return participation;
}

gnc::DynamicMode MakeMode(const std::complex<double> &rawEigenvalue,
    const Eigen::VectorXcd &eigenvector,
    const std::vector<std::string> &stateNames) {
  gnc::DynamicMode mode;
  const bool isReal = IsNearReal(rawEigenvalue);
  mode.eigenvalue = {rawEigenvalue.real(),
      isReal ? 0.0 : std::abs(rawEigenvalue.imag())};
  mode.naturalFrequencyRadPerSec =
      std::hypot(mode.eigenvalue.real(), mode.eigenvalue.imag());
  if (mode.naturalFrequencyRadPerSec > MinimumNaturalFrequency) {
    mode.dampingRatio =
        -mode.eigenvalue.real() / mode.naturalFrequencyRadPerSec;
  }
  if (!isReal && std::abs(mode.eigenvalue.imag()) > EigenvalueZeroTolerance) {
    mode.periodSec = 2.0 * std::numbers::pi / std::abs(mode.eigenvalue.imag());
  }

  if (mode.eigenvalue.real() < -EigenvalueZeroTolerance) {
    mode.stability = gnc::DynamicModeStability::Stable;
  } else if (mode.eigenvalue.real() > EigenvalueZeroTolerance) {
    mode.stability = gnc::DynamicModeStability::Unstable;
  }

  mode.stateParticipations = CalculateParticipation(eigenvector, stateNames);
  mode.classification = ClassifyMode(mode);
  return mode;
}
} // namespace

namespace gnc {
std::string_view ToString(DynamicModeClassification classification) {
  switch (classification) {
  case DynamicModeClassification::ShortPeriod:
    return "Short Period";
  case DynamicModeClassification::Phugoid:
    return "Phugoid";
  case DynamicModeClassification::Roll:
    return "Roll";
  case DynamicModeClassification::DutchRoll:
    return "Dutch Roll";
  case DynamicModeClassification::Spiral:
    return "Spiral";
  case DynamicModeClassification::Unknown:
  default:
    return "Unknown";
  }
}

std::string_view ToString(DynamicModeStability stability) {
  switch (stability) {
  case DynamicModeStability::Stable:
    return "Stable";
  case DynamicModeStability::Unstable:
    return "Unstable";
  case DynamicModeStability::Neutral:
  default:
    return "Neutral";
  }
}

DynamicModeAnalysis DynamicModeAnalyzer::Analyze(
    const LinearizationResult &linearization, double linearizationSimTimeSec) {
  DynamicModeAnalysis analysis;
  analysis.linearizationSimTimeSec = linearizationSimTimeSec;

  if (linearization.A.rows() == 0
      || linearization.A.rows() != linearization.A.cols()) {
    analysis.errorMessage =
        "The linearization A matrix is empty or not square.";
    return analysis;
  }
  if (!linearization.A.allFinite()) {
    analysis.errorMessage =
        "The linearization A matrix contains non-finite values.";
    return analysis;
  }

  Eigen::ComplexEigenSolver<Eigen::MatrixXd> solver;
  solver.compute(linearization.A, true);
  if (solver.info() != Eigen::Success) {
    analysis.errorMessage = "Eigenvalue decomposition of A failed.";
    return analysis;
  }

  const Eigen::VectorXcd &eigenvalues = solver.eigenvalues();
  const Eigen::MatrixXcd &eigenvectors = solver.eigenvectors();
  std::vector<bool> consumed(static_cast<std::size_t>(eigenvalues.size()),
      false);

  for (Eigen::Index index = 0; index < eigenvalues.size(); ++index) {
    const std::size_t logicalIndex = static_cast<std::size_t>(index);
    if (consumed[logicalIndex]) {
      continue;
    }

    consumed[logicalIndex] = true;
    Eigen::Index representativeIndex = index;
    if (!IsNearReal(eigenvalues(index))) {
      for (Eigen::Index candidate = index + 1; candidate < eigenvalues.size();
          ++candidate) {
        const std::size_t candidateIndex = static_cast<std::size_t>(candidate);
        if (consumed[candidateIndex]
            || !IsConjugatePair(eigenvalues(index), eigenvalues(candidate))) {
          continue;
        }

        consumed[candidateIndex] = true;
        if (eigenvalues(candidate).imag() > eigenvalues(index).imag()) {
          representativeIndex = candidate;
        }
        break;
      }
    }

    const std::complex<double> eigenvalue = eigenvalues(representativeIndex);
    if (!std::isfinite(eigenvalue.real())
        || !std::isfinite(eigenvalue.imag())) {
      analysis.errorMessage =
          "Eigenvalue decomposition produced a non-finite value.";
      analysis.modes.clear();
      return analysis;
    }
    analysis.modes.push_back(MakeMode(eigenvalue,
        eigenvectors.col(representativeIndex),
        linearization.stateNames));
  }

  if (analysis.modes.empty()) {
    analysis.errorMessage =
        "The linearization did not produce any dynamic modes.";
    return analysis;
  }

  analysis.valid = true;
  return analysis;
}
} // namespace gnc
