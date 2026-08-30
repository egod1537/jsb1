#pragma once

#include "gui/resources/EditorIcon.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gui {
class EditorIconRegistry {
public:
  // Texture lifetime
  EditorIconRegistry() = default;
  ~EditorIconRegistry();

  EditorIconRegistry(const EditorIconRegistry &other) = delete;
  EditorIconRegistry &operator=(const EditorIconRegistry &other) = delete;

  bool Initialize();
  void Shutdown();

  // Name-based icon access
  EditorIconHandle Get(std::string_view name);
  bool Contains(std::string_view name) const;

  // Indexed resource metadata
  const std::vector<EditorIconInfo> &GetIndex() const { return iconIndex_; }
  std::size_t GetLoadedCount() const { return loadedIcons_.size(); }
  const std::filesystem::path &GetResourceRoot() const { return resourceRoot_; }

private:
  struct Resource {
    EditorIconInfo info;
    std::filesystem::path sourcePath;
    std::string lookupKey;
    std::string stemKey;
  };

  struct Texture {
    ImTextureID id = ImTextureID_Invalid;
    unsigned int handle = 0;
    ImVec2 size{};
  };

  // Resource indexing
  std::filesystem::path FindResourceRoot() const;
  void BuildIndex();
  static std::string NormalizeLookupName(std::string_view name);

  // Lazy GPU loading
  EditorIconHandle Load(std::size_t resourceIndex);
  void LogMissingOnce(std::string_view name, const std::string &lookupKey);

  // Indexed disk resources
  std::filesystem::path resourceRoot_;
  std::vector<Resource> resources_;
  std::vector<EditorIconInfo> iconIndex_;
  std::unordered_map<std::string, std::size_t> resourceLookup_;

  // Cached runtime resources
  std::unordered_map<std::size_t, Texture> loadedIcons_;
  std::unordered_set<std::size_t> failedResources_;
  std::unordered_set<std::string> missingNames_;
  bool initialized_ = false;
};
} // namespace gui
