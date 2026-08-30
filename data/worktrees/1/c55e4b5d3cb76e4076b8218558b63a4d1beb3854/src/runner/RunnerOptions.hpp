#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace runner {
struct RunnerOptions {
  std::filesystem::path scenarioPath;
  std::filesystem::path outputDirectory;
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
