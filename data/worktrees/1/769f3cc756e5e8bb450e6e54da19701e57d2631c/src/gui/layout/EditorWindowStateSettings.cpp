#include "gui/layout/EditorWindowStateSettings.hpp"

#include "gui/Window.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>

namespace gui {
namespace {
constexpr const char *SettingsTypeName = "JSBWindow";

EditorWindowStateSettings *GetSettings(ImGuiSettingsHandler *handler) {
  return static_cast<EditorWindowStateSettings *>(handler->UserData);
}
} // namespace

bool EditorWindowStateSettings::Register(std::vector<Window *> &windows) {
  if (ImGui::GetCurrentContext() == nullptr
      || ImGui::FindSettingsHandler(SettingsTypeName) != nullptr) {
    return false;
  }
  windows_ = &windows;

  ImGuiSettingsHandler handler;
  handler.TypeName = SettingsTypeName;
  handler.TypeHash = ImHashStr(SettingsTypeName);
  handler.UserData = this;
  handler.ClearAllFn = [](ImGuiContext *, ImGuiSettingsHandler *entry) {
    auto *settings = GetSettings(entry);
    if (settings == nullptr || settings->windows_ == nullptr) {
      return;
    }
    for (Window *window : *settings->windows_) {
      if (window != nullptr) {
        window->SetVisible(true);
      }
    }
  };
  handler.ReadInitFn = handler.ClearAllFn;
  handler.ReadOpenFn = [](ImGuiContext *,
                           ImGuiSettingsHandler *entry,
                           const char *name) -> void * {
    auto *settings = GetSettings(entry);
    if (settings == nullptr || settings->windows_ == nullptr
        || name == nullptr) {
      return nullptr;
    }
    const auto window = std::find_if(settings->windows_->begin(),
        settings->windows_->end(),
        [name](const Window *candidate) {
          return candidate != nullptr && candidate->GetWindowId() == name;
        });
    return window == settings->windows_->end() ? nullptr : *window;
  };
  handler.ReadLineFn = [](ImGuiContext *,
                           ImGuiSettingsHandler *,
                           void *entry,
                           const char *line) {
    auto *window = static_cast<Window *>(entry);
    int visible = 1;
    if (window != nullptr && line != nullptr
        && std::sscanf(line, "Visible=%d", &visible) == 1) {
      window->SetVisible(visible != 0);
    }
  };
  handler.WriteAllFn =
      [](ImGuiContext *, ImGuiSettingsHandler *entry, ImGuiTextBuffer *output) {
        auto *settings = GetSettings(entry);
        if (settings == nullptr || settings->windows_ == nullptr
            || output == nullptr) {
          return;
        }
        for (const Window *window : *settings->windows_) {
          if (window == nullptr) {
            continue;
          }
          output->appendf("[%s][%s]\nVisible=%d\n\n",
              SettingsTypeName,
              window->GetWindowId().c_str(),
              window->IsVisible() ? 1 : 0);
        }
      };
  ImGui::AddSettingsHandler(&handler);
  return true;
}
} // namespace gui
