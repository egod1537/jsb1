#include "RunnerOptions.hpp"

#include <iostream>
#include <utility>

namespace runner {
namespace {
bool TakeValue(const std::vector<std::string_view> &arguments,
    std::size_t &index, std::string_view option, std::string_view &value,
    std::string &error) {
  if (index + 1 >= arguments.size() || arguments[index + 1].empty()) {
    error = std::string(option) + " requires a value";
    return false;
  }
  value = arguments[++index];
  return true;
}
} // namespace

RunnerParseResult ParseRunnerOptions(
    const std::vector<std::string_view> &arguments) {
  RunnerParseResult result;
  RunnerOptions options;
  bool scenarioSet = false;
  bool outputSet = false;
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    const std::string_view argument = arguments[index];
    if (argument == "--help" || argument == "-h") {
      result.helpRequested = true;
      continue;
    }
    std::string_view value;
    if (argument == "--scenario") {
      if (!TakeValue(arguments, index, argument, value, result.error)) {
        return result;
      }
      if (scenarioSet) {
        result.error = "--scenario may only be specified once";
        return result;
      }
      options.scenarioPath = value;
      scenarioSet = true;
    } else if (argument == "--output") {
      if (!TakeValue(arguments, index, argument, value, result.error)) {
        return result;
      }
      if (outputSet) {
        result.error = "--output may only be specified once";
        return result;
      }
      options.outputDirectory = value;
      outputSet = true;
    } else if (argument == "--variant" || argument == "--mode") {
      result.error = std::string(argument)
                     + " is not supported; headless execution always runs "
                       "baseline and primary together";
      return result;
    } else if (argument == "--autopilot" || argument == "--aircraft"
               || argument == "--dt" || argument == "--duration"
               || argument == "--no-trim") {
      result.error = std::string(argument)
                     + " cannot be used with --scenario; execution semantics "
                       "are defined by the scenario";
      return result;
    } else {
      result.error = "unknown option: " + std::string(argument);
      return result;
    }
  }

  if (result.helpRequested) {
    return result;
  }
  if (!scenarioSet) {
    result.error = "--scenario is required";
    return result;
  }
  if (!outputSet) {
    result.error = "--output is required";
    return result;
  }
  result.options = std::move(options);
  return result;
}

void PrintRunnerHelp() {
  std::cout
      << "Usage:\n"
         "  jsb-sim-runner --scenario <file> --output <directory>\n\n"
         "Options:\n"
         "  --scenario <path>          Scenario YAML file\n"
         "  --output <path>            Output directory\n"
         "                             Runs baseline and primary together\n"
         "  --help                     Show this help\n";
}
} // namespace runner
