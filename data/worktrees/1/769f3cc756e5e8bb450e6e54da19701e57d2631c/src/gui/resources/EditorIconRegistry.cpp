#include "gui/resources/EditorIconRegistry.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <system_error>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace gui {
namespace {
constexpr std::string_view AssetRelativeDirectory =
    "assets/third_party/unity-editor-icons/icons/small";
constexpr GLint TextureClampToEdge = 0x812F;

bool IsDirectory(const std::filesystem::path &path) {
  std::error_code error;
  return std::filesystem::is_directory(path, error) && !error;
}

std::filesystem::path GetExecutableDirectory() {
#if defined(_WIN32)
  std::array<wchar_t, 32768> pathBuffer{};
  const DWORD length = GetModuleFileNameW(nullptr,
      pathBuffer.data(),
      static_cast<DWORD>(pathBuffer.size()));
  if (length > 0 && length < pathBuffer.size()) {
    return std::filesystem::path(std::wstring_view(pathBuffer.data(), length))
        .parent_path();
  }
#elif defined(__linux__)
  std::error_code error;
  const std::filesystem::path executablePath =
      std::filesystem::read_symlink("/proc/self/exe", error);
  if (!error) {
    return executablePath.parent_path();
  }
#endif
  return {};
}

bool IsPngFile(const std::filesystem::path &path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(),
      extension.end(),
      extension.begin(),
      [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
  return extension == ".png";
}
} // namespace

EditorIconRegistry::~EditorIconRegistry() { Shutdown(); }

bool EditorIconRegistry::Initialize() {
  if (initialized_) {
    return !resources_.empty();
  }

  initialized_ = true;
  resourceRoot_ = FindResourceRoot();
  if (resourceRoot_.empty()) {
    std::cerr << "Unable to locate the Unity editor icon resource root\n";
    return false;
  }

  BuildIndex();
  return !resources_.empty();
}

void EditorIconRegistry::Shutdown() {
  for (auto &entry : loadedIcons_) {
    Texture &texture = entry.second;
    if (texture.handle != 0) {
      glDeleteTextures(1, &texture.handle);
    }
  }

  loadedIcons_.clear();
  failedResources_.clear();
  missingNames_.clear();
  resourceLookup_.clear();
  iconIndex_.clear();
  resources_.clear();
  resourceRoot_.clear();
  initialized_ = false;
}

EditorIconHandle EditorIconRegistry::Get(std::string_view name) {
  if (!initialized_ && !Initialize()) {
    return {};
  }

  const std::string lookupKey = NormalizeLookupName(name);
  const auto resource = resourceLookup_.find(lookupKey);
  if (resource == resourceLookup_.end()) {
    LogMissingOnce(name, lookupKey);
    return {};
  }

  const std::size_t resourceIndex = resource->second;
  const auto loaded = loadedIcons_.find(resourceIndex);
  if (loaded != loadedIcons_.end()) {
    return {loaded->second.id, loaded->second.size};
  }
  if (failedResources_.contains(resourceIndex)) {
    return {};
  }

  return Load(resourceIndex);
}

bool EditorIconRegistry::Contains(std::string_view name) const {
  return resourceLookup_.contains(NormalizeLookupName(name));
}

std::filesystem::path EditorIconRegistry::FindResourceRoot() const {
  const std::filesystem::path executableDirectory = GetExecutableDirectory();
  if (!executableDirectory.empty()) {
    const std::filesystem::path packagedPath =
        executableDirectory / AssetRelativeDirectory;
    if (IsDirectory(packagedPath)) {
      return packagedPath;
    }
  }

  std::error_code error;
  const std::filesystem::path currentDirectory =
      std::filesystem::current_path(error);
  if (!error) {
    const std::filesystem::path workingDirectoryPath =
        currentDirectory / AssetRelativeDirectory;
    if (IsDirectory(workingDirectoryPath)) {
      return workingDirectoryPath;
    }
  }

#ifdef JSB_UNITY_EDITOR_ICON_DIRECTORY
  const std::filesystem::path sourcePath = JSB_UNITY_EDITOR_ICON_DIRECTORY;
  if (IsDirectory(sourcePath)) {
    return sourcePath;
  }
#endif

  return {};
}

void EditorIconRegistry::BuildIndex() {
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator
           iterator(resourceRoot_, error),
      end;
      iterator != end && !error;
      iterator.increment(error)) {
    const bool regularFile = iterator->is_regular_file(error);
    if (error) {
      error.clear();
      continue;
    }
    if (!regularFile || !IsPngFile(iterator->path())) {
      continue;
    }

    std::filesystem::path relativePath =
        std::filesystem::relative(iterator->path(), resourceRoot_, error);
    if (error) {
      error.clear();
      continue;
    }

    relativePath.replace_extension();
    Resource resource;
    resource.info.name = iterator->path().stem().string();
    resource.info.relativeName = relativePath.generic_string();
    resource.sourcePath = iterator->path();
    resource.lookupKey = NormalizeLookupName(resource.info.relativeName);
    resource.stemKey = NormalizeLookupName(resource.info.name);
    resources_.push_back(std::move(resource));
  }

  std::ranges::sort(resources_, {}, [](const Resource &resource) {
    return resource.lookupKey;
  });

  std::unordered_map<std::string, std::size_t> stemCounts;
  for (const Resource &resource : resources_) {
    ++stemCounts[resource.stemKey];
  }

  iconIndex_.reserve(resources_.size());
  resourceLookup_.reserve(resources_.size() * 2);
  for (std::size_t index = 0; index < resources_.size(); ++index) {
    const Resource &resource = resources_[index];
    iconIndex_.push_back(resource.info);
    resourceLookup_.try_emplace(resource.lookupKey, index);
    if (stemCounts[resource.stemKey] == 1) {
      resourceLookup_.try_emplace(resource.stemKey, index);
    }
  }
}

std::string EditorIconRegistry::NormalizeLookupName(std::string_view name) {
  std::string result(name);
  std::replace(result.begin(), result.end(), '\\', '/');
  std::ranges::transform(result, result.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });

  while (result.starts_with("./")) {
    result.erase(0, 2);
  }
  if (result.starts_with("icons/small/")) {
    result.erase(0, std::string_view("icons/small/").size());
  } else if (result.starts_with("small/")) {
    result.erase(0, std::string_view("small/").size());
  }
  if (result.ends_with(".png")) {
    result.erase(result.size() - 4);
  }
  return result;
}

EditorIconHandle EditorIconRegistry::Load(std::size_t resourceIndex) {
  const Resource &resource = resources_[resourceIndex];
  int width = 0;
  int height = 0;
  int channelCount = 0;
  stbi_uc *pixels = stbi_load(resource.sourcePath.string().c_str(),
      &width,
      &height,
      &channelCount,
      4);
  if (pixels == nullptr || width <= 0 || height <= 0) {
    std::cerr << "Unable to load editor icon from " << resource.sourcePath
              << '\n';
    stbi_image_free(pixels);
    failedResources_.insert(resourceIndex);
    return {};
  }

  GLint previousTexture = 0;
  GLint previousUnpackAlignment = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);

  GLuint handle = 0;
  glGenTextures(1, &handle);
  if (handle == 0) {
    stbi_image_free(pixels);
    failedResources_.insert(resourceIndex);
    return {};
  }

  glBindTexture(GL_TEXTURE_2D, handle);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, TextureClampToEdge);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, TextureClampToEdge);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D,
      0,
      GL_RGBA,
      width,
      height,
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      pixels);

  glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
  stbi_image_free(pixels);

  Texture texture;
  texture.id = static_cast<ImTextureID>(handle);
  texture.handle = handle;
  texture.size = ImVec2(static_cast<float>(width), static_cast<float>(height));
  const auto [loaded, inserted] = loadedIcons_.emplace(resourceIndex, texture);
  static_cast<void>(inserted);
  return {loaded->second.id, loaded->second.size};
}

void EditorIconRegistry::LogMissingOnce(std::string_view name,
    const std::string &lookupKey) {
  if (missingNames_.insert(lookupKey).second) {
    std::cerr << "Unknown editor icon: " << name << '\n';
  }
}
} // namespace gui
