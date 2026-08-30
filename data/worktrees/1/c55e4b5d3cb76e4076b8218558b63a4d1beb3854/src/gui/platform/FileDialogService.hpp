#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace gui {
struct FileDialogFilter {
  std::string displayName;
  std::string pattern;
};

class IFileDialog {
public:
  virtual ~IFileDialog() = default;

  virtual std::optional<std::filesystem::path> OpenFile(std::string_view title,
      const FileDialogFilter &filter) = 0;
  virtual std::optional<std::filesystem::path> SaveFile(std::string_view title,
      const FileDialogFilter &filter, std::string_view suggestedFileName) = 0;
  virtual const std::string &GetLastError() const = 0;
};

class NativeFileDialogService final : public IFileDialog {
public:
  std::optional<std::filesystem::path> OpenFile(std::string_view title,
      const FileDialogFilter &filter) override;
  std::optional<std::filesystem::path> SaveFile(std::string_view title,
      const FileDialogFilter &filter,
      std::string_view suggestedFileName) override;
  const std::string &GetLastError() const override;

private:
  std::string lastError_;
};
} // namespace gui
