#include "gui/features/simulation/ScenarioController.hpp"
#include "common/crypto/Sha256.hpp"

#include "sim/scenario/SimulationScenarioSerializer.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace gui {
namespace {
constexpr const char *DefaultScenarioFileName = "roll_hold_5deg_30s.yaml";

std::filesystem::path FindDefaultScenarioDirectory() {
  std::error_code error;
  std::filesystem::path directory = std::filesystem::current_path(error);
  if (error) {
    return std::filesystem::path("scenarios");
  }

  while (!directory.empty()) {
    if (std::filesystem::exists(directory / "CMakeLists.txt", error) && !error
        && std::filesystem::exists(directory / "src", error) && !error) {
      return directory / "scenarios";
    }
    const std::filesystem::path parent = directory.parent_path();
    if (parent == directory) {
      break;
    }
    directory = parent;
  }
  return std::filesystem::current_path() / "scenarios";
}

bool HasYamlExtension(const std::filesystem::path &path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(),
      extension.end(),
      extension.begin(),
      [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
  return extension == ".yaml" || extension == ".yml";
}
} // namespace

ScenarioController::ScenarioController(std::filesystem::path scenarioDirectory,
    architecture::EventSink<ScenarioLaunchRequested> parentEvents)
    : parentEvents_(std::move(parentEvents)) {
  model_.directory = scenarioDirectory.empty() ? FindDefaultScenarioDirectory()
                                               : std::move(scenarioDirectory);
  model_.suggestedFileName = DefaultScenarioFileName;
  RefreshAvailableScenarios();
  const std::filesystem::path defaultScenario =
      model_.directory / DefaultScenarioFileName;
  std::error_code error;
  if (std::filesystem::is_regular_file(defaultScenario, error) && !error) {
    Load(defaultScenario);
  }
}

bool ScenarioController::IsDirty() const {
  return model_.draft != model_.cleanScenario;
}

void ScenarioController::Handle(const ScenarioDraftChanged &event) {
  model_.draft = event.draft;
}

void ScenarioController::Handle(const ExecutionVariantChanged &event) {
  model_.executionVariant = event.variant;
}

void ScenarioController::NewScenario() {
  model_.draft = sim::SimulationScenario{};
  model_.cleanScenario = model_.draft;
  model_.currentFilePath.clear();
  model_.source = {};
  model_.executionVariant = sim::ExecutionVariant::Primary;
  model_.suggestedFileName = "untitled.yaml";
  SetStatus("Created a new scenario. Use Save As to persist it.", false);
}

void ScenarioController::ResetDefaults() {
  model_.draft = sim::SimulationScenario{};
}

void ScenarioController::RefreshAvailableScenarios() {
  model_.availableScenarioFiles.clear();
  std::error_code error;
  for (std::filesystem::directory_iterator iterator(model_.directory, error),
      end;
      !error && iterator != end;
      iterator.increment(error)) {
    if (iterator->is_regular_file(error)
        && HasYamlExtension(iterator->path())) {
      model_.availableScenarioFiles.push_back(iterator->path());
    }
  }
  std::sort(model_.availableScenarioFiles.begin(),
      model_.availableScenarioFiles.end(),
      [](const std::filesystem::path &left,
          const std::filesystem::path &right) {
        return left.filename().string() < right.filename().string();
      });
}

bool ScenarioController::Load(const std::filesystem::path &path) {
  const std::filesystem::path resolvedPath = ResolvePath(path);
  sim::SimulationScenario loadedScenario;
  sim::ScenarioLoadMetadata loadMetadata;
  std::string error;
  if (!sim::SimulationScenarioSerializer::Load(resolvedPath,
          loadedScenario,
          error,
          &loadMetadata)) {
    SetStatus(std::move(error), true);
    return false;
  }

  model_.draft = std::move(loadedScenario);
  model_.cleanScenario = model_.draft;
  model_.currentFilePath = resolvedPath;
  model_.suggestedFileName = resolvedPath.filename().string();
  model_.source = {
      .file = resolvedPath.string(),
      .digestSha256 = loadMetadata.sourceDigestSha256,
  };
  RefreshAvailableScenarios();
  model_.executionVariant =
      loadMetadata.legacyVariant.value_or(sim::ExecutionVariant::Primary);
  if (loadMetadata.warnings.empty()) {
    SetStatus("Loaded " + resolvedPath.filename().string(), false);
  } else {
    SetStatus("Loaded " + resolvedPath.filename().string() + ". "
                  + loadMetadata.warnings.front(),
        false);
  }
  return true;
}

bool ScenarioController::Save() {
  if (model_.currentFilePath.empty()) {
    SetStatus("No scenario file is connected. Use Save As.", true);
    return false;
  }
  return SaveAs(model_.currentFilePath);
}

bool ScenarioController::SaveAs(const std::filesystem::path &path) {
  const std::filesystem::path resolvedPath = ResolvePath(path);
  std::string error;
  if (!sim::SimulationScenarioSerializer::Save(resolvedPath,
          model_.draft,
          error)) {
    SetStatus(std::move(error), true);
    return false;
  }

  model_.source.file = resolvedPath.string();
  model_.source.digestSha256 = common::crypto::Sha256Hex(
      sim::SimulationScenarioSerializer::Serialize(model_.draft));
  model_.cleanScenario = model_.draft;
  model_.currentFilePath = resolvedPath;
  model_.suggestedFileName = resolvedPath.filename().string();
  RefreshAvailableScenarios();
  SetStatus("Saved " + resolvedPath.filename().string(), false);
  return true;
}

bool ScenarioController::ResolveFileName(std::string_view input,
    std::filesystem::path &path) {
  path = std::filesystem::path(input);
  if (path.empty()) {
    SetStatus("Enter a scenario file name.", true);
    return false;
  }
  if (path.has_parent_path()) {
    SetStatus("Scenario File accepts a file name, not a directory path.", true);
    return false;
  }
  if (path.extension().empty()) {
    path.replace_extension(".yaml");
  }
  if (!HasYamlExtension(path)) {
    SetStatus("Scenario file extension must be .yaml or .yml.", true);
    return false;
  }
  return true;
}

bool ScenarioController::Apply() {
  std::string validationError;
  if (!sim::ValidateSimulationScenario(model_.draft, &validationError)) {
    model_.lastApplySucceeded = false;
    SetStatus(std::move(validationError), true);
    return false;
  }
  if (!parentEvents_.IsConnected()) {
    model_.lastApplySucceeded = false;
    SetStatus("Scenario runtime connection is unavailable.", true);
    return false;
  }

  model_.applyPending = true;
  model_.lastApplySucceeded = false;
  parentEvents_.Emit(ScenarioLaunchRequested{sim::ExecutionRequest{
      .scenario = model_.draft,
      .variant = model_.executionVariant,
      .source = model_.source,
  }});
  if (model_.applyPending) {
    model_.applyPending = false;
    SetStatus("Scenario runtime did not report an apply result.", true);
  }
  return model_.lastApplySucceeded;
}

void ScenarioController::Handle(const ScenarioApplyCompleted &event) {
  model_.applyPending = false;
  model_.lastApplySucceeded = event.succeeded;
  if (event.succeeded) {
    SetStatus("Applied "
                  + (model_.draft.name.empty() ? std::string("Scenario")
                                               : model_.draft.name),
        false);
    return;
  }
  SetStatus(event.error.empty() ? "Scenario apply failed." : event.error, true);
}

std::filesystem::path ScenarioController::ResolvePath(
    const std::filesystem::path &path) const {
  std::filesystem::path resolved =
      path.is_absolute() ? path : model_.directory / path;
  if (resolved.extension().empty()) {
    resolved.replace_extension(".yaml");
  }
  return resolved.lexically_normal();
}

void ScenarioController::SetStatus(std::string message, bool error) {
  model_.statusMessage = std::move(message);
  model_.statusIsError = error;
}
} // namespace gui
