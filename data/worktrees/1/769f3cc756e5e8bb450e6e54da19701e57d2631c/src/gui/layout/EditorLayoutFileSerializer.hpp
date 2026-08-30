#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace gui {
inline constexpr std::string_view EditorLayoutFileFormat = "jsb-editor-layout";
inline constexpr int EditorLayoutFileVersion = 1;
inline constexpr std::uintmax_t MaximumEditorLayoutFileSize =
    4U * 1024U * 1024U;

struct EditorLayoutExportData {
  int version = EditorLayoutFileVersion;
  std::string name;
  std::string imguiIni;
};

class EditorLayoutFileSerializer {
public:
  // Portable file serialization
  static bool Serialize(const EditorLayoutExportData &data, std::string &json,
      std::string &error);
  static bool Deserialize(std::string_view json, EditorLayoutExportData &data,
      std::string &error);

  // Bounded file I/O
  static bool Load(const std::filesystem::path &path,
      EditorLayoutExportData &data, std::string &error);
  static bool Save(const std::filesystem::path &path,
      const EditorLayoutExportData &data, std::string &error);
};
} // namespace gui
