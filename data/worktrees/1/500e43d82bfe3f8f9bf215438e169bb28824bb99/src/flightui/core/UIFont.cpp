#include "flightui/core/UIFont.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <iostream>
#include <system_error>

namespace FlightUI {
namespace {
constexpr const char *InterRelativePath = "assets/fonts/Inter-Regular.ttf";

bool IsRegularFile(const std::filesystem::path &path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error) && !error;
}

ImFont *AddFallbackFont(ImGuiIO &io) {
  io.Fonts->ClearFonts();
  ImFontConfig config;
  config.SizePixels = BaseUIFontSize;
  return io.Fonts->AddFontDefault(&config);
}

const ImWchar *GetPrimaryGlyphRanges(ImFontAtlas &atlas) {
  static ImVector<ImWchar> ranges;
  ImFontGlyphRangesBuilder builder;
  builder.AddRanges(atlas.GetGlyphRangesGreek());
  builder.AddChar(0x0307); // Combining dot above for p-dot, q-dot, and r-dot.
  ranges.clear();
  builder.BuildRanges(&ranges);
  return ranges.Data;
}
} // namespace

std::filesystem::path GetPrimaryUIFontPath() {
  std::error_code error;
  const std::filesystem::path currentDirectory =
      std::filesystem::current_path(error);
  if (!error) {
    const std::filesystem::path packagedPath =
        currentDirectory / InterRelativePath;
    if (IsRegularFile(packagedPath)) {
      return packagedPath;
    }
  }

#ifdef JSB_INTER_FONT_FILE
  const std::filesystem::path sourcePath = JSB_INTER_FONT_FILE;
  if (IsRegularFile(sourcePath)) {
    return sourcePath;
  }
#endif

  return error ? std::filesystem::path(InterRelativePath)
               : currentDirectory / InterRelativePath;
}

bool LoadPrimaryUIFont() {
  ImGuiIO &io = ImGui::GetIO();
  const std::filesystem::path fontPath = GetPrimaryUIFontPath();
  ImFont *font = nullptr;

  if (IsRegularFile(fontPath)) {
    io.Fonts->ClearFonts();
    ImFontConfig config;
    config.Flags |= ImFontFlags_NoLoadError;
    std::snprintf(config.Name, sizeof(config.Name), "Inter Regular");
    font = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(),
        BaseUIFontSize,
        &config,
        GetPrimaryGlyphRanges(*io.Fonts));
  }
  const bool loadedInter = font != nullptr;

  if (font == nullptr) {
    font = AddFallbackFont(io);
    std::cerr << "Unable to load Inter UI font from " << fontPath
              << "; using the default ImGui font\n";
  }

  io.FontDefault = font;
  ImGui::GetStyle().FontSizeBase = BaseUIFontSize;
  return loadedInter;
}

float CalculateUIFontScale(float uiScale) {
  if (!std::isfinite(uiScale) || uiScale <= 0.0F) {
    return 1.0F;
  }

  return std::max(uiScale, MinimumUIFontSize / BaseUIFontSize);
}
} // namespace FlightUI
