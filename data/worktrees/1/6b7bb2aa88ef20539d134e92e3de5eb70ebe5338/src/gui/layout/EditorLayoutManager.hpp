#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gui {
using LayoutPresetId = std::string;

struct EditorLayoutPreset {
  LayoutPresetId id;
  std::string name;
  std::filesystem::path relativeFile;
};

class IEditorLayoutBackend {
public:
  virtual ~IEditorLayoutBackend() = default;

  virtual std::string CaptureLayoutIni() const = 0;
  virtual bool ApplyLayoutIni(std::string_view imguiIni) = 0;
};

class ImGuiEditorLayoutBackend final : public IEditorLayoutBackend {
public:
  std::string CaptureLayoutIni() const override;
  bool ApplyLayoutIni(std::string_view imguiIni) override;
};

class EditorLayoutManager {
public:
  explicit EditorLayoutManager(std::filesystem::path editorConfigDirectory,
      IEditorLayoutBackend *backend = nullptr);

  // Storage lifecycle
  bool Initialize();
  const std::filesystem::path &GetEditorConfigDirectory() const;
  const std::filesystem::path &GetWorkspaceIniPath() const;

  // Preset access and ordering
  const std::vector<EditorLayoutPreset> &GetPresets() const;
  const EditorLayoutPreset *FindPreset(const LayoutPresetId &id) const;
  const std::optional<LayoutPresetId> &GetActivePresetId() const;
  void ClearActivePreset();
  bool MovePreset(const LayoutPresetId &id, std::size_t destinationIndex);

  // Preset lifecycle
  bool SaveCurrentLayout(std::string name, LayoutPresetId *createdId = nullptr);
  bool CreatePreset(std::string name, std::string imguiIni,
      LayoutPresetId *createdId = nullptr);
  bool ApplyPreset(const LayoutPresetId &id);
  bool UpdatePreset(const LayoutPresetId &id);
  bool RenamePreset(const LayoutPresetId &id, std::string name);
  bool DeletePreset(const LayoutPresetId &id);

  // Portable import and export
  bool ExportPreset(const LayoutPresetId &id,
      const std::filesystem::path &destination);
  bool ImportPreset(const std::filesystem::path &source,
      LayoutPresetId *importedId = nullptr);

  // Presentation helpers and diagnostics
  static std::filesystem::path GetDefaultEditorConfigDirectory();
  static std::string MakeSuggestedExportFileName(std::string_view name);
  const std::string &GetLastError() const;

private:
  // Metadata persistence
  bool LoadMetadata(std::vector<EditorLayoutPreset> &presets);
  bool PersistMetadata(const std::vector<EditorLayoutPreset> &presets);

  // Snapshot persistence
  bool ReadPresetIni(const EditorLayoutPreset &preset, std::string &imguiIni);
  bool WritePresetIni(const EditorLayoutPreset &preset,
      std::string_view imguiIni);
  bool PersistNewPreset(const EditorLayoutPreset &preset,
      std::string_view imguiIni,
      const std::vector<EditorLayoutPreset> &updatedPresets);

  // Validation and identity
  static bool IsValidName(std::string_view name);
  static bool IsValidIni(std::string_view imguiIni);
  std::string MakeUniqueName(std::string_view requestedName) const;
  LayoutPresetId GeneratePresetId() const;
  bool HasPresetId(std::string_view id) const;

  // Lookup and errors
  std::vector<EditorLayoutPreset>::iterator FindPresetIterator(
      const LayoutPresetId &id);
  std::vector<EditorLayoutPreset>::const_iterator FindPresetIterator(
      const LayoutPresetId &id) const;
  bool Fail(std::string message);
  void ClearError();

  // Storage paths
  std::filesystem::path editorConfigDirectory_;
  std::filesystem::path workspaceIniPath_;
  std::filesystem::path layoutsDirectory_;
  std::filesystem::path presetsDirectory_;
  std::filesystem::path metadataPath_;

  // Runtime state
  std::vector<EditorLayoutPreset> presets_;
  std::optional<LayoutPresetId> activePresetId_;
  std::string lastError_;
  IEditorLayoutBackend *backend_ = nullptr;
};
} // namespace gui
