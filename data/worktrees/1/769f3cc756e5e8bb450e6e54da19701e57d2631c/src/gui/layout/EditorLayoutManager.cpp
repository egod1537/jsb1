#include "gui/layout/EditorLayoutManager.hpp"

#include "gui/layout/EditorLayoutFileSerializer.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

namespace gui {
namespace {
constexpr std::size_t MaximumLayoutNameLength = 256;

void AppendEscapedJsonString(std::string_view value, std::string &output) {
  constexpr char Hex[] = "0123456789abcdef";
  output.push_back('"');
  for (const unsigned char character : value) {
    switch (character) {
    case '"':
      output += "\\\"";
      break;
    case '\\':
      output += "\\\\";
      break;
    case '\b':
      output += "\\b";
      break;
    case '\f':
      output += "\\f";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\t':
      output += "\\t";
      break;
    default:
      if (character < 0x20U) {
        output += "\\u00";
        output.push_back(Hex[(character >> 4U) & 0x0FU]);
        output.push_back(Hex[character & 0x0FU]);
      } else {
        output.push_back(static_cast<char>(character));
      }
      break;
    }
  }
  output.push_back('"');
}

bool WriteTextFile(const std::filesystem::path &path, std::string_view content,
    std::string &error) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "Could not open file for writing: " + path.string();
    return false;
  }
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!output) {
    error = "Could not write file: " + path.string();
    return false;
  }
  return true;
}

std::string MakeTemporarySuffix() {
  static std::uint64_t counter = 0;
  const auto ticks =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return ".tmp-" + std::to_string(ticks) + "-" + std::to_string(++counter);
}

bool ReplaceFileAtomically(const std::filesystem::path &path,
    std::string_view content, std::string &error) {
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::error_code filesystemError;
    std::filesystem::create_directories(parent, filesystemError);
    if (filesystemError) {
      error = "Could not create layout storage directory: "
              + filesystemError.message();
      return false;
    }
  }

  const std::string suffix = MakeTemporarySuffix();
  const std::filesystem::path temporary = path.string() + suffix;
  if (!WriteTextFile(temporary, content, error)) {
    return false;
  }

  std::error_code filesystemError;
  if (!std::filesystem::exists(path, filesystemError)) {
    std::filesystem::rename(temporary, path, filesystemError);
    if (!filesystemError) {
      return true;
    }
    std::filesystem::remove(temporary);
    error = "Could not commit layout file: " + filesystemError.message();
    return false;
  }
  if (filesystemError) {
    std::filesystem::remove(temporary);
    error = "Could not inspect layout file: " + filesystemError.message();
    return false;
  }

  const std::filesystem::path backup = path.string() + suffix + ".bak";
  std::filesystem::rename(path, backup, filesystemError);
  if (filesystemError) {
    std::filesystem::remove(temporary);
    error =
        "Could not prepare layout file update: " + filesystemError.message();
    return false;
  }
  std::filesystem::rename(temporary, path, filesystemError);
  if (filesystemError) {
    std::error_code rollbackError;
    std::filesystem::rename(backup, path, rollbackError);
    std::filesystem::remove(temporary);
    error = "Could not commit layout file: " + filesystemError.message();
    return false;
  }
  std::filesystem::remove(backup, filesystemError);
  return true;
}

bool ReadBoundedTextFile(const std::filesystem::path &path,
    std::string &content, std::string &error) {
  std::error_code filesystemError;
  const std::uintmax_t size = std::filesystem::file_size(path, filesystemError);
  if (filesystemError) {
    error = "Could not read layout snapshot: " + filesystemError.message();
    return false;
  }
  if (size > MaximumEditorLayoutFileSize) {
    error = "Layout snapshot exceeds the 4 MiB size limit";
    return false;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "Could not open layout snapshot";
    return false;
  }
  content.assign(static_cast<std::size_t>(size), '\0');
  if (size > 0) {
    input.read(content.data(), static_cast<std::streamsize>(size));
  }
  if (!input) {
    error = "Could not read layout snapshot";
    return false;
  }
  return true;
}

std::string ReadRequiredScalar(const YAML::Node &node, const char *key) {
  const YAML::Node value = node[key];
  if (!value || !value.IsScalar()) {
    throw std::runtime_error(std::string("missing or invalid field: ") + key);
  }
  return value.as<std::string>();
}

std::filesystem::path EnvironmentPath(const char *name) {
  const char *value = std::getenv(name);
  return value == nullptr || *value == '\0' ? std::filesystem::path{}
                                            : std::filesystem::path(value);
}
} // namespace

std::string ImGuiEditorLayoutBackend::CaptureLayoutIni() const {
  std::size_t size = 0;
  const char *settings = ImGui::SaveIniSettingsToMemory(&size);
  return settings == nullptr ? std::string{} : std::string(settings, size);
}

bool ImGuiEditorLayoutBackend::ApplyLayoutIni(std::string_view imguiIni) {
  if (imguiIni.empty() || ImGui::GetCurrentContext() == nullptr) {
    return false;
  }
  ImGui::LoadIniSettingsFromMemory(imguiIni.data(), imguiIni.size());
  ImGui::MarkIniSettingsDirty();
  return true;
}

EditorLayoutManager::EditorLayoutManager(
    std::filesystem::path editorConfigDirectory, IEditorLayoutBackend *backend)
    : editorConfigDirectory_(std::move(editorConfigDirectory)),
      workspaceIniPath_(editorConfigDirectory_ / "imgui.ini"),
      layoutsDirectory_(editorConfigDirectory_ / "layouts"),
      presetsDirectory_(layoutsDirectory_ / "presets"),
      metadataPath_(layoutsDirectory_ / "layouts.json"), backend_(backend) {}

bool EditorLayoutManager::Initialize() {
  std::error_code filesystemError;
  std::filesystem::create_directories(presetsDirectory_, filesystemError);
  if (filesystemError) {
    return Fail("Could not create editor layout directory: "
                + filesystemError.message());
  }

  std::vector<EditorLayoutPreset> loaded;
  if (!LoadMetadata(loaded)) {
    return false;
  }
  presets_ = std::move(loaded);
  activePresetId_.reset();
  ClearError();
  return true;
}

const std::filesystem::path &
EditorLayoutManager::GetEditorConfigDirectory() const {
  return editorConfigDirectory_;
}

const std::filesystem::path &EditorLayoutManager::GetWorkspaceIniPath() const {
  return workspaceIniPath_;
}

const std::vector<EditorLayoutPreset> &EditorLayoutManager::GetPresets() const {
  return presets_;
}

const EditorLayoutPreset *EditorLayoutManager::FindPreset(
    const LayoutPresetId &id) const {
  const auto preset = FindPresetIterator(id);
  return preset == presets_.end() ? nullptr : &*preset;
}

const std::optional<LayoutPresetId> &
EditorLayoutManager::GetActivePresetId() const {
  return activePresetId_;
}

void EditorLayoutManager::ClearActivePreset() { activePresetId_.reset(); }

bool EditorLayoutManager::MovePreset(const LayoutPresetId &id,
    std::size_t destinationIndex) {
  const auto preset = FindPresetIterator(id);
  if (preset == presets_.end()) {
    return Fail("Layout preset was not found");
  }
  if (destinationIndex >= presets_.size()) {
    return Fail("Layout preset destination is out of range");
  }
  const std::size_t sourceIndex =
      static_cast<std::size_t>(std::distance(presets_.begin(), preset));
  if (sourceIndex == destinationIndex) {
    ClearError();
    return true;
  }

  std::vector<EditorLayoutPreset> reordered = presets_;
  EditorLayoutPreset moved = std::move(reordered[sourceIndex]);
  reordered.erase(reordered.begin() + static_cast<std::ptrdiff_t>(sourceIndex));
  reordered.insert(reordered.begin()
                       + static_cast<std::ptrdiff_t>(destinationIndex),
      std::move(moved));
  if (!PersistMetadata(reordered)) {
    return false;
  }
  presets_ = std::move(reordered);
  ClearError();
  return true;
}

bool EditorLayoutManager::SaveCurrentLayout(std::string name,
    LayoutPresetId *createdId) {
  if (backend_ == nullptr) {
    return Fail("Layout backend is unavailable");
  }
  return CreatePreset(std::move(name), backend_->CaptureLayoutIni(), createdId);
}

bool EditorLayoutManager::CreatePreset(std::string name, std::string imguiIni,
    LayoutPresetId *createdId) {
  if (!IsValidName(name)) {
    return Fail("Layout name must contain 1 to 256 bytes");
  }
  if (!IsValidIni(imguiIni)) {
    return Fail("Layout snapshot must not be empty or exceed 4 MiB");
  }

  name = MakeUniqueName(name);
  const LayoutPresetId id = GeneratePresetId();
  const EditorLayoutPreset preset{
      .id = id,
      .name = std::move(name),
      .relativeFile = std::filesystem::path("presets") / (id + ".ini"),
  };
  std::vector<EditorLayoutPreset> updated = presets_;
  updated.push_back(preset);
  if (!PersistNewPreset(preset, imguiIni, updated)) {
    return false;
  }
  presets_ = std::move(updated);
  if (createdId != nullptr) {
    *createdId = id;
  }
  ClearError();
  return true;
}

bool EditorLayoutManager::ApplyPreset(const LayoutPresetId &id) {
  const EditorLayoutPreset *preset = FindPreset(id);
  if (preset == nullptr) {
    return Fail("Layout preset was not found");
  }
  if (backend_ == nullptr) {
    return Fail("Layout backend is unavailable");
  }
  std::string imguiIni;
  if (!ReadPresetIni(*preset, imguiIni)) {
    return false;
  }
  if (!backend_->ApplyLayoutIni(imguiIni)) {
    return Fail("Could not apply layout preset");
  }
  activePresetId_ = id;
  ClearError();
  return true;
}

bool EditorLayoutManager::UpdatePreset(const LayoutPresetId &id) {
  const EditorLayoutPreset *preset = FindPreset(id);
  if (preset == nullptr) {
    return Fail("Layout preset was not found");
  }
  if (backend_ == nullptr) {
    return Fail("Layout backend is unavailable");
  }
  const std::string imguiIni = backend_->CaptureLayoutIni();
  if (!IsValidIni(imguiIni)) {
    return Fail("Layout snapshot must not be empty or exceed 4 MiB");
  }
  if (!WritePresetIni(*preset, imguiIni)) {
    return false;
  }
  ClearError();
  return true;
}

bool EditorLayoutManager::RenamePreset(const LayoutPresetId &id,
    std::string name) {
  const auto preset = FindPresetIterator(id);
  if (preset == presets_.end()) {
    return Fail("Layout preset was not found");
  }
  if (!IsValidName(name)) {
    return Fail("Layout name must contain 1 to 256 bytes");
  }
  const bool collision = std::any_of(presets_.begin(),
      presets_.end(),
      [&](const EditorLayoutPreset &candidate) {
        return candidate.id != id && candidate.name == name;
      });
  if (collision) {
    return Fail("A layout preset with that name already exists");
  }

  std::vector<EditorLayoutPreset> updated = presets_;
  const auto updatedPreset = std::find_if(updated.begin(),
      updated.end(),
      [&](const EditorLayoutPreset &candidate) { return candidate.id == id; });
  updatedPreset->name = std::move(name);
  if (!PersistMetadata(updated)) {
    return false;
  }
  presets_ = std::move(updated);
  ClearError();
  return true;
}

bool EditorLayoutManager::DeletePreset(const LayoutPresetId &id) {
  const auto preset = FindPresetIterator(id);
  if (preset == presets_.end()) {
    return Fail("Layout preset was not found");
  }
  const std::filesystem::path snapshot =
      layoutsDirectory_ / preset->relativeFile;
  std::vector<EditorLayoutPreset> updated = presets_;
  updated.erase(std::remove_if(updated.begin(),
                    updated.end(),
                    [&](const EditorLayoutPreset &candidate) {
                      return candidate.id == id;
                    }),
      updated.end());
  if (!PersistMetadata(updated)) {
    return false;
  }
  presets_ = std::move(updated);
  if (activePresetId_ == id) {
    activePresetId_.reset();
  }
  std::error_code ignored;
  std::filesystem::remove(snapshot, ignored);
  ClearError();
  return true;
}

bool EditorLayoutManager::ExportPreset(const LayoutPresetId &id,
    const std::filesystem::path &destination) {
  const EditorLayoutPreset *preset = FindPreset(id);
  if (preset == nullptr) {
    return Fail("Layout preset was not found");
  }
  std::string imguiIni;
  if (!ReadPresetIni(*preset, imguiIni)) {
    return false;
  }
  std::string error;
  if (!EditorLayoutFileSerializer::Save(destination,
          EditorLayoutExportData{
              .version = EditorLayoutFileVersion,
              .name = preset->name,
              .imguiIni = std::move(imguiIni),
          },
          error)) {
    return Fail(std::move(error));
  }
  ClearError();
  return true;
}

bool EditorLayoutManager::ImportPreset(const std::filesystem::path &source,
    LayoutPresetId *importedId) {
  EditorLayoutExportData data;
  std::string error;
  if (!EditorLayoutFileSerializer::Load(source, data, error)) {
    return Fail(std::move(error));
  }

  const LayoutPresetId id = GeneratePresetId();
  const EditorLayoutPreset preset{
      .id = id,
      .name = MakeUniqueName(data.name),
      .relativeFile = std::filesystem::path("presets") / (id + ".ini"),
  };
  std::vector<EditorLayoutPreset> updated = presets_;
  updated.push_back(preset);
  if (!PersistNewPreset(preset, data.imguiIni, updated)) {
    return false;
  }
  presets_ = std::move(updated);
  if (importedId != nullptr) {
    *importedId = id;
  }
  ClearError();
  return true;
}

std::filesystem::path EditorLayoutManager::GetDefaultEditorConfigDirectory() {
#if defined(_WIN32)
  const std::filesystem::path appData = EnvironmentPath("APPDATA");
  if (!appData.empty()) {
    return appData / "jsb-flight-console" / "editor";
  }
#else
  const std::filesystem::path xdgConfig = EnvironmentPath("XDG_CONFIG_HOME");
  if (!xdgConfig.empty()) {
    return xdgConfig / "jsb-flight-console" / "editor";
  }
  const std::filesystem::path home = EnvironmentPath("HOME");
  if (!home.empty()) {
    return home / ".config" / "jsb-flight-console" / "editor";
  }
#endif
  return std::filesystem::current_path() / "config" / "editor";
}

std::string EditorLayoutManager::MakeSuggestedExportFileName(
    std::string_view name) {
  std::string stem;
  bool pendingSeparator = false;
  for (const unsigned char character : name) {
    if ((character >= 'a' && character <= 'z')
        || (character >= '0' && character <= '9')) {
      if (pendingSeparator && !stem.empty()) {
        stem.push_back('-');
      }
      pendingSeparator = false;
      stem.push_back(static_cast<char>(character));
    } else if (character >= 'A' && character <= 'Z') {
      if (pendingSeparator && !stem.empty()) {
        stem.push_back('-');
      }
      pendingSeparator = false;
      stem.push_back(static_cast<char>(character - 'A' + 'a'));
    } else {
      pendingSeparator = true;
    }
  }
  if (stem.empty()) {
    stem = "layout";
  }
  return stem + ".layout.json";
}

const std::string &EditorLayoutManager::GetLastError() const {
  return lastError_;
}

bool EditorLayoutManager::LoadMetadata(
    std::vector<EditorLayoutPreset> &presets) {
  presets.clear();
  std::error_code filesystemError;
  if (!std::filesystem::exists(metadataPath_, filesystemError)) {
    if (filesystemError) {
      return Fail(
          "Could not inspect layout metadata: " + filesystemError.message());
    }
    return true;
  }

  try {
    const YAML::Node root = YAML::LoadFile(metadataPath_.string());
    const YAML::Node items = root["presets"];
    if (!root.IsMap() || !items || !items.IsSequence()) {
      return Fail("Layout metadata has an invalid structure");
    }
    for (const YAML::Node &item : items) {
      if (!item.IsMap()) {
        return Fail("Layout metadata contains an invalid preset");
      }
      EditorLayoutPreset preset{
          .id = ReadRequiredScalar(item, "id"),
          .name = ReadRequiredScalar(item, "name"),
          .relativeFile = ReadRequiredScalar(item, "file"),
      };
      const std::filesystem::path expected =
          std::filesystem::path("presets") / (preset.id + ".ini");
      if (preset.id.empty() || !IsValidName(preset.name)
          || preset.relativeFile != expected
          || std::any_of(presets.begin(),
              presets.end(),
              [&](const EditorLayoutPreset &existing) {
                return existing.id == preset.id;
              })) {
        return Fail("Layout metadata contains an invalid preset");
      }
      presets.push_back(std::move(preset));
    }
  } catch (const std::exception &exception) {
    return Fail(
        "Could not parse layout metadata: " + std::string(exception.what()));
  }
  return true;
}

bool EditorLayoutManager::PersistMetadata(
    const std::vector<EditorLayoutPreset> &presets) {
  std::string json = "{\n  \"presets\": [";
  for (std::size_t index = 0; index < presets.size(); ++index) {
    const EditorLayoutPreset &preset = presets[index];
    json += index == 0 ? "\n" : ",\n";
    json += "    {\n      \"id\": ";
    AppendEscapedJsonString(preset.id, json);
    json += ",\n      \"name\": ";
    AppendEscapedJsonString(preset.name, json);
    json += ",\n      \"file\": ";
    AppendEscapedJsonString(preset.relativeFile.generic_string(), json);
    json += "\n    }";
  }
  json += presets.empty() ? "]\n}\n" : "\n  ]\n}\n";

  std::string error;
  if (!ReplaceFileAtomically(metadataPath_, json, error)) {
    return Fail(std::move(error));
  }
  return true;
}

bool EditorLayoutManager::ReadPresetIni(const EditorLayoutPreset &preset,
    std::string &imguiIni) {
  std::string error;
  if (!ReadBoundedTextFile(layoutsDirectory_ / preset.relativeFile,
          imguiIni,
          error)) {
    return Fail(std::move(error));
  }
  if (!IsValidIni(imguiIni)) {
    return Fail("Stored layout snapshot is empty or too large");
  }
  return true;
}

bool EditorLayoutManager::WritePresetIni(const EditorLayoutPreset &preset,
    std::string_view imguiIni) {
  std::string error;
  if (!ReplaceFileAtomically(layoutsDirectory_ / preset.relativeFile,
          imguiIni,
          error)) {
    return Fail(std::move(error));
  }
  return true;
}

bool EditorLayoutManager::PersistNewPreset(const EditorLayoutPreset &preset,
    std::string_view imguiIni,
    const std::vector<EditorLayoutPreset> &updatedPresets) {
  const std::filesystem::path snapshot =
      layoutsDirectory_ / preset.relativeFile;
  std::error_code filesystemError;
  if (std::filesystem::exists(snapshot, filesystemError) || filesystemError) {
    return Fail("Generated layout preset ID collides with local storage");
  }
  std::string error;
  if (!ReplaceFileAtomically(snapshot, imguiIni, error)) {
    return Fail(std::move(error));
  }
  if (!PersistMetadata(updatedPresets)) {
    std::filesystem::remove(snapshot, filesystemError);
    return false;
  }
  return true;
}

bool EditorLayoutManager::IsValidName(std::string_view name) {
  return !name.empty() && name.size() <= MaximumLayoutNameLength
         && std::any_of(name.begin(), name.end(), [](unsigned char character) {
              return character != ' ' && character != '\t' && character != '\r'
                     && character != '\n';
            });
}

bool EditorLayoutManager::IsValidIni(std::string_view imguiIni) {
  return !imguiIni.empty() && imguiIni.size() <= MaximumEditorLayoutFileSize;
}

std::string EditorLayoutManager::MakeUniqueName(
    std::string_view requestedName) const {
  const auto exists = [&](std::string_view name) {
    return std::any_of(presets_.begin(),
        presets_.end(),
        [&](const EditorLayoutPreset &preset) { return preset.name == name; });
  };
  if (!exists(requestedName)) {
    return std::string(requestedName);
  }
  for (std::size_t copy = 2;; ++copy) {
    std::string candidate =
        std::string(requestedName) + " (" + std::to_string(copy) + ")";
    if (!exists(candidate)) {
      return candidate;
    }
  }
}

LayoutPresetId EditorLayoutManager::GeneratePresetId() const {
  std::random_device randomDevice;
  std::mt19937_64 random(randomDevice());
  for (;;) {
    std::array<std::uint32_t, 4> words{};
    for (std::uint32_t &word : words) {
      word = static_cast<std::uint32_t>(random());
    }
    std::ostringstream id;
    id << std::hex << std::setfill('0') << std::setw(8) << words[0] << '-'
       << std::setw(4) << (words[1] >> 16U) << '-' << std::setw(4)
       << (words[1] & 0xFFFFU) << '-' << std::setw(4) << (words[2] >> 16U)
       << '-' << std::setw(4) << (words[2] & 0xFFFFU) << std::setw(8)
       << words[3];
    if (!HasPresetId(id.str())) {
      return id.str();
    }
  }
}

bool EditorLayoutManager::HasPresetId(std::string_view id) const {
  return std::any_of(presets_.begin(),
      presets_.end(),
      [&](const EditorLayoutPreset &preset) { return preset.id == id; });
}

std::vector<EditorLayoutPreset>::iterator
EditorLayoutManager::FindPresetIterator(const LayoutPresetId &id) {
  return std::find_if(presets_.begin(),
      presets_.end(),
      [&](const EditorLayoutPreset &preset) { return preset.id == id; });
}

std::vector<EditorLayoutPreset>::const_iterator
EditorLayoutManager::FindPresetIterator(const LayoutPresetId &id) const {
  return std::find_if(presets_.begin(),
      presets_.end(),
      [&](const EditorLayoutPreset &preset) { return preset.id == id; });
}

bool EditorLayoutManager::Fail(std::string message) {
  lastError_ = std::move(message);
  return false;
}

void EditorLayoutManager::ClearError() { lastError_.clear(); }
} // namespace gui
