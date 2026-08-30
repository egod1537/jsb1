#include "gui/windows/EditorIconBrowserWindow.hpp"

#include "gui/resources/EditorIconRegistry.hpp"
#include "flightui/core/UIScale.hpp"

#include <algorithm>
#include <cctype>
#include <imgui.h>

namespace gui {
namespace {
constexpr float PreviewSize = 24.0F;
constexpr float RowHeight = 34.0F;

std::string Lowercase(std::string_view value) {
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return result;
}
} // namespace

EditorIconBrowserWindow::EditorIconBrowserWindow(EditorIconRegistry &icons)
    : Window("Editor Icons"), icons_(icons) {
  SetVisible(false);
}

void EditorIconBrowserWindow::OnRender(const sim::SimulationSnapshot &) {
  const std::vector<EditorIconInfo> &iconIndex = icons_.GetIndex();

  ImGui::SetNextItemWidth(
      std::min(FlightUI::Ui(380.0F), ImGui::GetContentRegionAvail().x * 0.6F));
  const bool searchChanged = ImGui::InputTextWithHint("##EditorIconSearch",
      "Search icon name or relative path",
      searchText_.data(),
      searchText_.size());
  if (searchChanged || indexedResourceCount_ != iconIndex.size()) {
    RefreshFilter(iconIndex);
  }

  ImGui::SameLine();
  ImGui::TextDisabled("%zu / %zu icons, %zu loaded",
      filteredIcons_.size(),
      iconIndex.size(),
      icons_.GetLoadedCount());

  constexpr ImGuiTableFlags TableFlags =
      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg
      | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
  if (!ImGui::BeginTable("EditorIconBrowserTable",
          3,
          TableFlags,
          ImGui::GetContentRegionAvail())) {
    return;
  }

  ImGui::TableSetupColumn("Preview",
      ImGuiTableColumnFlags_WidthFixed,
      FlightUI::Ui(54.0F));
  ImGui::TableSetupColumn("Asset Name",
      ImGuiTableColumnFlags_WidthFixed,
      FlightUI::Ui(240.0F));
  ImGui::TableSetupColumn("Relative Name", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableHeadersRow();

  const float previewSize = FlightUI::Ui(PreviewSize);
  const float rowHeight = FlightUI::Ui(RowHeight);
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(filteredIcons_.size()), rowHeight);
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
      const EditorIconInfo &info = iconIndex[filteredIcons_[row]];
      ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);

      ImGui::TableSetColumnIndex(0);
      const EditorIconHandle icon = icons_.Get(info.relativeName);
      if (icon.IsValid()) {
        const float aspectRatio = icon.size.x / icon.size.y;
        const ImVec2 imageSize =
            aspectRatio >= 1.0F
                ? ImVec2(previewSize, previewSize / aspectRatio)
                : ImVec2(previewSize * aspectRatio, previewSize);
        ImGui::ImageWithBg(ImTextureRef(icon.texture),
            imageSize,
            ImVec2(0.0F, 0.0F),
            ImVec2(1.0F, 1.0F),
            ImVec4(0.0F, 0.0F, 0.0F, 0.0F),
            ImGui::GetStyleColorVec4(ImGuiCol_Text));
      } else {
        ImGui::TextDisabled("-");
      }

      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(info.name.c_str());

      ImGui::TableSetColumnIndex(2);
      ImGui::TextDisabled("%s", info.relativeName.c_str());
    }
  }

  ImGui::EndTable();
}

void EditorIconBrowserWindow::RefreshFilter(
    const std::vector<EditorIconInfo> &iconIndex) {
  appliedSearch_ = Lowercase(searchText_.data());
  indexedResourceCount_ = iconIndex.size();
  filteredIcons_.clear();
  filteredIcons_.reserve(iconIndex.size());

  for (std::size_t index = 0; index < iconIndex.size(); ++index) {
    const EditorIconInfo &info = iconIndex[index];
    if (appliedSearch_.empty()
        || Lowercase(info.name).find(appliedSearch_) != std::string::npos
        || Lowercase(info.relativeName).find(appliedSearch_)
               != std::string::npos) {
      filteredIcons_.push_back(index);
    }
  }
}
} // namespace gui
