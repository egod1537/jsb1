#pragma once

#include <Eigen/Dense>
#include <optional>
#include <vector>
#include <string>

namespace gnc {
struct LinearizationResult {
  Eigen::MatrixXd A;
  Eigen::MatrixXd B;

  Eigen::VectorXd x0;
  Eigen::VectorXd u0;

  std::vector<std::string> stateNames;
  std::vector<std::string> inputNames;

  std::optional<std::size_t> FindStateIndex(std::string_view name) const;
  std::optional<std::size_t> FindInputIndex(std::string_view name) const;
};
} // namespace gnc
