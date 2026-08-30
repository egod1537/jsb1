#pragma once

#include "gui/Window.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace gui {
class EditorIconBrowserWindow final : public Window {
public:
  explicit EditorIconBrowserWindow(EditorIconRegistry &icons);

protected:
  void OnRender(const sim::SimulationSnapshot &snapshot) override;

private:
  // Search and filtering
  void RefreshFilter(const std::vector<EditorIconInfo> &iconIndex);

  EditorIconRegistry &icons_;
  std::array<char, 256> searchText_{};
  std::string appliedSearch_;
  std::vector<std::size_t> filteredIcons_;
  std::size_t indexedResourceCount_ = 0;
};
} // namespace gui
