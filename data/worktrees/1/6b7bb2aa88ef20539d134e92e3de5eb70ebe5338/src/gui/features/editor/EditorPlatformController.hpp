#pragma once

#include "gui/layout/EditorLayoutManager.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gui {
class IFileDialog;

struct EditorLayoutOperationResult {
  bool succeeded = false;
  bool canceled = false;
  LayoutPresetId presetId;
  std::string presetName;
  std::string error;
};

class EditorPlatformController {
public:
  EditorPlatformController(EditorLayoutManager &layouts,
      IFileDialog &fileDialog, std::function<void()> resetLayout);

  // Immutable layout props for the toolbar view
  const std::vector<EditorLayoutPreset> &GetPresets() const;
  const std::optional<LayoutPresetId> &GetActivePresetId() const;
  const EditorLayoutPreset *FindPreset(const LayoutPresetId &id) const;

  // Layout intent handling
  EditorLayoutOperationResult ApplyLayout(const LayoutPresetId &id);
  EditorLayoutOperationResult SaveLayout(std::string_view name);
  EditorLayoutOperationResult MoveLayout(const LayoutPresetId &id,
      std::size_t destinationIndex);
  EditorLayoutOperationResult UpdateLayout(const LayoutPresetId &id);
  EditorLayoutOperationResult DeleteLayout(const LayoutPresetId &id);
  EditorLayoutOperationResult RenameLayout(const LayoutPresetId &id,
      std::string_view name);
  EditorLayoutOperationResult ImportLayout();
  EditorLayoutOperationResult ExportLayout(const LayoutPresetId &id);
  void ResetLayout();

private:
  EditorLayoutManager &layouts_;
  IFileDialog &fileDialog_;
  std::function<void()> resetLayout_;
};
} // namespace gui
