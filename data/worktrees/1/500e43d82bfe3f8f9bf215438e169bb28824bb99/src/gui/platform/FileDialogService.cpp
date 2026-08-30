#include "gui/platform/FileDialogService.hpp"

#include <algorithm>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <commdlg.h>

#include <array>
#endif

namespace gui {
namespace {
#if defined(_WIN32)
std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty()) {
    return {};
  }
  const int length = MultiByteToWideChar(CP_UTF8,
      MB_ERR_INVALID_CHARS,
      value.data(),
      static_cast<int>(value.size()),
      nullptr,
      0);
  if (length <= 0) {
    return {};
  }
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8,
      MB_ERR_INVALID_CHARS,
      value.data(),
      static_cast<int>(value.size()),
      result.data(),
      length);
  return result;
}

std::wstring MakeFilter(const FileDialogFilter &filter) {
  std::wstring result = Utf8ToWide(filter.displayName);
  result.push_back(L'\0');
  result += Utf8ToWide(filter.pattern);
  result.push_back(L'\0');
  result += L"All Files (*.*)";
  result.push_back(L'\0');
  result += L"*.*";
  result.push_back(L'\0');
  result.push_back(L'\0');
  return result;
}

std::string DialogError(DWORD code) {
  return code == 0
             ? std::string{}
             : "Native file dialog failed with code " + std::to_string(code);
}
#endif
} // namespace

std::optional<std::filesystem::path> NativeFileDialogService::OpenFile(
    std::string_view title, const FileDialogFilter &filter) {
  lastError_.clear();
#if defined(_WIN32)
  std::array<wchar_t, 32768> path{};
  const std::wstring wideTitle = Utf8ToWide(title);
  const std::wstring wideFilter = MakeFilter(filter);
  OPENFILENAMEW options{};
  options.lStructSize = sizeof(options);
  options.lpstrFile = path.data();
  options.nMaxFile = static_cast<DWORD>(path.size());
  options.lpstrFilter = wideFilter.c_str();
  options.lpstrTitle = wideTitle.c_str();
  options.Flags =
      OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameW(&options) == TRUE) {
    return std::filesystem::path(path.data());
  }
  lastError_ = DialogError(CommDlgExtendedError());
  return std::nullopt;
#else
  (void)title;
  (void)filter;
  lastError_ = "Native file dialogs are unavailable on this platform";
  return std::nullopt;
#endif
}

std::optional<std::filesystem::path> NativeFileDialogService::SaveFile(
    std::string_view title, const FileDialogFilter &filter,
    std::string_view suggestedFileName) {
  lastError_.clear();
#if defined(_WIN32)
  std::array<wchar_t, 32768> path{};
  const std::wstring suggested = Utf8ToWide(suggestedFileName);
  std::copy_n(suggested.begin(),
      std::min(suggested.size(), path.size() - 1),
      path.begin());
  const std::wstring wideTitle = Utf8ToWide(title);
  const std::wstring wideFilter = MakeFilter(filter);
  OPENFILENAMEW options{};
  options.lStructSize = sizeof(options);
  options.lpstrFile = path.data();
  options.nMaxFile = static_cast<DWORD>(path.size());
  options.lpstrFilter = wideFilter.c_str();
  options.lpstrTitle = wideTitle.c_str();
  options.lpstrDefExt = L"layout.json";
  options.Flags =
      OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetSaveFileNameW(&options) == TRUE) {
    std::filesystem::path selected(path.data());
    const std::wstring selectedName = selected.filename().wstring();
    constexpr std::wstring_view Extension = L".layout.json";
    if (selectedName.size() < Extension.size()
        || selectedName.substr(selectedName.size() - Extension.size())
               != Extension) {
      selected += Extension;
    }
    return selected;
  }
  lastError_ = DialogError(CommDlgExtendedError());
  return std::nullopt;
#else
  (void)title;
  (void)filter;
  (void)suggestedFileName;
  lastError_ = "Native file dialogs are unavailable on this platform";
  return std::nullopt;
#endif
}

const std::string &NativeFileDialogService::GetLastError() const {
  return lastError_;
}
} // namespace gui
