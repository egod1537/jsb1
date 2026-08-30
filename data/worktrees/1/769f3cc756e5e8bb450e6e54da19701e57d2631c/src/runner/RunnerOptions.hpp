#pragma once

#include "ExecutionMode.hpp"
#include "sim/execution/ExecutionVariant.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace runner {
struct RunnerOptions {
  std::filesystem::path scenarioPath;
  std::filesystem::path outputDirectory;
  ExecutionMode mode = ExecutionMode::Single;
  std::optional<sim::ExecutionVariant> variant;
};

struct RunnerParseResult {
  std::optional<RunnerOptions> options;
  bool helpRequested = false;
  std::string error;
};

RunnerParseResult ParseRunnerOptions(
    const std::vector<std::string_view> &arguments);
void PrintRunnerHelp();
} // namespace runner
