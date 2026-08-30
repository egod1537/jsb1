#include "gui/features/editor/EditorPlatformController.hpp"

#include "gui/platform/FileDialogService.hpp"

#include <filesystem>
#include <utility>

namespace gui {
namespace {
const FileDialogFilter LayoutFileFilter{
    .displayName = "JSB Editor Layout (*.layout.json)",
    .pattern = "*.layout.json",
};
} // namespace

EditorPlatformController::EditorPlatformController(EditorLayoutManager &layouts,
    IFileDialog &fileDialog, std::function<void()> resetLayout)
    : layouts_(layouts), fileDialog_(fileDialog),
      resetLayout_(std::move(resetLayout)) {}

const std::vector<EditorLayoutPreset> &
EditorPlatformController::GetPresets() const {
  return layouts_.GetPresets();
}

const std::optional<LayoutPresetId> &
EditorPlatformController::GetActivePresetId() const {
  return layouts_.GetActivePresetId();
}

const EditorLayoutPreset *EditorPlatformController::FindPreset(
    const LayoutPresetId &id) const {
  return layouts_.FindPreset(id);
}

EditorLayoutOperationResult EditorPlatformController::ApplyLayout(
    const LayoutPresetId &id) {
  const EditorLayoutPreset *preset = layouts_.FindPreset(id);
  const std::string name = preset == nullptr ? "Layout" : preset->name;
  if (!layouts_.ApplyPreset(id)) {
    return {.presetId = id,
        .presetName = name,
        .error = layouts_.GetLastError()};
  }
  return {.succeeded = true, .presetId = id, .presetName = name};
}

EditorLayoutOperationResult EditorPlatformController::SaveLayout(
    std::string_view name) {
  LayoutPresetId id;
  if (!layouts_.SaveCurrentLayout(std::string(name), &id)) {
    return {.error = layouts_.GetLastError()};
  }
  const EditorLayoutPreset *preset = layouts_.FindPreset(id);
  return {.succeeded = true,
      .presetId = id,
      .presetName = preset == nullptr ? std::string(name) : preset->name};
}

EditorLayoutOperationResult EditorPlatformController::MoveLayout(
    const LayoutPresetId &id, std::size_t destinationIndex) {
  if (!layouts_.MovePreset(id, destinationIndex)) {
    return {.presetId = id, .error = layouts_.GetLastError()};
  }
  return {.succeeded = true, .presetId = id};
}

EditorLayoutOperationResult EditorPlatformController::UpdateLayout(
    const LayoutPresetId &id) {
  const EditorLayoutPreset *preset = layouts_.FindPreset(id);
  const std::string name = preset == nullptr ? "Layout" : preset->name;
  if (!layouts_.UpdatePreset(id)) {
    return {.presetId = id,
        .presetName = name,
        .error = layouts_.GetLastError()};
  }
  return {.succeeded = true, .presetId = id, .presetName = name};
}

EditorLayoutOperationResult EditorPlatformController::DeleteLayout(
    const LayoutPresetId &id) {
  const EditorLayoutPreset *preset = layouts_.FindPreset(id);
  const std::string name = preset == nullptr ? "Layout" : preset->name;
  if (!layouts_.DeletePreset(id)) {
    return {.presetId = id,
        .presetName = name,
        .error = layouts_.GetLastError()};
  }
  return {.succeeded = true, .presetId = id, .presetName = name};
}

EditorLayoutOperationResult EditorPlatformController::RenameLayout(
    const LayoutPresetId &id, std::string_view name) {
  if (!layouts_.RenamePreset(id, std::string(name))) {
    return {.presetId = id, .error = layouts_.GetLastError()};
  }
  const EditorLayoutPreset *preset = layouts_.FindPreset(id);
  return {.succeeded = true,
      .presetId = id,
      .presetName = preset == nullptr ? std::string(name) : preset->name};
}

EditorLayoutOperationResult EditorPlatformController::ImportLayout() {
  const std::optional<std::filesystem::path> source =
      fileDialog_.OpenFile("Import Layout", LayoutFileFilter);
  if (!source.has_value()) {
    return {
        .canceled = fileDialog_.GetLastError().empty(),
        .error = fileDialog_.GetLastError(),
    };
  }

  LayoutPresetId importedId;
  if (!layouts_.ImportPreset(*source, &importedId)) {
    return {.error = layouts_.GetLastError()};
  }
  const EditorLayoutPreset *preset = layouts_.FindPreset(importedId);
  return {
      .succeeded = true,
      .presetId = importedId,
      .presetName = preset == nullptr ? "Layout" : preset->name,
  };
}

EditorLayoutOperationResult EditorPlatformController::ExportLayout(
    const LayoutPresetId &id) {
  const EditorLayoutPreset *preset = layouts_.FindPreset(id);
  if (preset == nullptr) {
    return {.error = "preset was not found"};
  }
  const std::string presetName = preset->name;
  const std::optional<std::filesystem::path> destination =
      fileDialog_.SaveFile("Export Layout",
          LayoutFileFilter,
          EditorLayoutManager::MakeSuggestedExportFileName(presetName));
  if (!destination.has_value()) {
    return {
        .canceled = fileDialog_.GetLastError().empty(),
        .presetName = presetName,
        .error = fileDialog_.GetLastError(),
    };
  }
  if (!layouts_.ExportPreset(id, *destination)) {
    return {.presetName = presetName, .error = layouts_.GetLastError()};
  }
  return {.succeeded = true, .presetName = presetName};
}

void EditorPlatformController::ResetLayout() { resetLayout_(); }
} // namespace gui
