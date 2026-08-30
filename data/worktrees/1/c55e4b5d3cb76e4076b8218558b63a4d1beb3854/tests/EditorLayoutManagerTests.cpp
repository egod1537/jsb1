#include "gui/layout/EditorLayoutFileSerializer.hpp"
#include "gui/layout/EditorLayoutManager.hpp"
#include "gui/layout/EditorWindowStateSettings.hpp"
#include "gui/Window.hpp"

#include <imgui.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
class TestWindow final : public gui::Window {
public:
  TestWindow(std::string title, std::string id)
      : Window(std::move(title), {}, std::move(id)) {}

private:
  void OnRender(const sim::SimulationSnapshot &) override {}
};

class FakeLayoutBackend final : public gui::IEditorLayoutBackend {
public:
  std::string CaptureLayoutIni() const override { return currentIni; }

  bool ApplyLayoutIni(std::string_view imguiIni) override {
    ++applyCount;
    currentIni = std::string(imguiIni);
    return applySucceeds;
  }

  std::string currentIni = "[Window][Initial]\nPos=1,2\n";
  int applyCount = 0;
  bool applySucceeds = true;
};

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    const auto ticks =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path()
            / ("jsb-layout-tests-" + std::to_string(ticks));
    std::filesystem::create_directories(path_);
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  const std::filesystem::path &Get() const { return path_; }

private:
  std::filesystem::path path_;
};

bool Expect(bool condition, std::string_view message) {
  if (condition) {
    return true;
  }
  std::cerr << "FAILED: " << message << '\n';
  return false;
}

bool WriteText(const std::filesystem::path &path, std::string_view text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(output);
}

bool TestExportRoundTripAndNoMutation() {
  TemporaryDirectory temporary;
  FakeLayoutBackend backend;
  gui::EditorLayoutManager manager(temporary.Get() / "editor", &backend);
  if (!Expect(manager.Initialize(), "manager initializes")) {
    return false;
  }

  const std::string originalIni =
      "[Window][Flight Viz Primary###FlightVizPrimary]\nPos=10,20\n"
      "Size=800,400\n";
  backend.currentIni = originalIni;
  gui::LayoutPresetId originalId;
  bool passed =
      Expect(manager.SaveCurrentLayout("Compare", &originalId),
          "save current layout as preset")
      && Expect(manager.GetPresets().size() == 1, "one preset before export");
  const gui::EditorLayoutPreset beforeExport = manager.GetPresets().front();
  const std::filesystem::path exportFile =
      temporary.Get() / "compare.layout.json";
  passed =
      Expect(manager.ExportPreset(originalId, exportFile), "export succeeds")
      && passed;
  passed = Expect(manager.GetPresets().size() == 1
                      && manager.GetPresets().front().id == beforeExport.id
                      && manager.GetPresets().front().name == beforeExport.name,
               "export does not mutate preset")
           && passed;
  passed =
      Expect(manager.DeletePreset(originalId), "delete local preset") && passed;

  gui::LayoutPresetId importedId;
  passed = Expect(manager.ImportPreset(exportFile, &importedId),
               "round-trip import succeeds")
           && passed;
  passed = Expect(importedId != originalId, "import generates a new local ID")
           && passed;
  passed = Expect(manager.GetPresets().size() == 1
                      && manager.GetPresets().front().name == "Compare",
               "round-trip preserves name")
           && passed;
  backend.currentIni = "changed";
  passed = Expect(manager.ApplyPreset(importedId), "imported preset applies")
           && passed;
  passed = Expect(backend.currentIni == originalIni,
               "round-trip preserves ImGui ini content")
           && passed;
  return passed;
}

bool TestPresetLifecycle() {
  TemporaryDirectory temporary;
  FakeLayoutBackend backend;
  gui::EditorLayoutManager manager(temporary.Get() / "editor", &backend);
  bool passed = Expect(manager.Initialize(), "lifecycle manager initializes");
  gui::LayoutPresetId id;
  passed =
      Expect(manager.SaveCurrentLayout("Working", &id), "save current layout")
      && passed;
  passed =
      Expect(manager.RenamePreset(id, "Analysis"), "rename preset") && passed;
  passed = Expect(manager.FindPreset(id) != nullptr
                      && manager.FindPreset(id)->name == "Analysis",
               "rename updates metadata")
           && passed;
  backend.currentIni = "[Window][Updated]\nPos=3,4\n";
  passed = Expect(manager.UpdatePreset(id), "update preset snapshot") && passed;
  backend.currentIni = "workspace sentinel";
  passed = Expect(manager.ApplyPreset(id), "apply uses manager path") && passed;
  passed = Expect(backend.currentIni == "[Window][Updated]\nPos=3,4\n"
                      && backend.applyCount == 1,
               "updated preset applies through backend")
           && passed;
  return passed;
}

bool TestDuplicateImportIdsNamesAndOrdering() {
  TemporaryDirectory temporary;
  FakeLayoutBackend backend;
  gui::EditorLayoutManager manager(temporary.Get() / "editor", &backend);
  bool passed = Expect(manager.Initialize(), "duplicate manager initializes");
  gui::LayoutPresetId firstId;
  gui::LayoutPresetId secondId;
  gui::LayoutPresetId thirdId;
  passed = Expect(manager.CreatePreset("Control", "control ini", &firstId),
               "create first ordered preset")
           && passed;
  passed = Expect(manager.CreatePreset("Compare", "compare local", &secondId),
               "create colliding local preset")
           && passed;

  const std::filesystem::path importFile =
      temporary.Get() / "external.layout.json";
  std::string error;
  passed = Expect(gui::EditorLayoutFileSerializer::Save(importFile,
                      gui::EditorLayoutExportData{
                          .name = "Compare",
                          .imguiIni = "compare imported",
                      },
                      error),
               "write duplicate import fixture")
           && passed;
  passed = Expect(manager.ImportPreset(importFile, &thirdId),
               "first duplicate import succeeds")
           && passed;
  gui::LayoutPresetId fourthId;
  passed = Expect(manager.ImportPreset(importFile, &fourthId),
               "same file imports twice")
           && passed;

  const auto &presets = manager.GetPresets();
  passed = Expect(presets.size() == 4, "imports append to the preset list")
           && passed;
  passed = Expect(presets[0].name == "Control" && presets[1].name == "Compare"
                      && presets[2].name == "Compare (2)"
                      && presets[3].name == "Compare (3)",
               "duplicate names receive sequential copy suffixes")
           && passed;
  passed =
      Expect(thirdId != fourthId && thirdId != secondId && fourthId != secondId,
          "every import receives a distinct local ID")
      && passed;
  passed = Expect(manager.MovePreset(fourthId, 0), "preset reorder succeeds")
           && passed;
  passed = Expect(manager.GetPresets()[0].id == fourthId
                      && manager.GetPresets()[1].id == firstId,
               "stored order drives F-key priority")
           && passed;

  FakeLayoutBackend reloadedBackend;
  gui::EditorLayoutManager reloaded(temporary.Get() / "editor",
      &reloadedBackend);
  passed =
      Expect(reloaded.Initialize(), "reloaded manager initializes") && passed;
  passed = Expect(reloaded.GetPresets().size() == 4
                      && reloaded.GetPresets()[0].id == fourthId,
               "import and reorder metadata persist")
           && passed;
  return passed;
}

bool TestInvalidImportsAreTransactional() {
  TemporaryDirectory temporary;
  FakeLayoutBackend backend;
  gui::EditorLayoutManager manager(temporary.Get() / "editor", &backend);
  bool passed = Expect(manager.Initialize(), "invalid manager initializes");
  gui::LayoutPresetId originalId;
  passed = Expect(manager.CreatePreset("Existing", "existing ini", &originalId),
               "create transaction sentinel")
           && passed;
  const std::size_t originalCount = manager.GetPresets().size();
  const std::string workspaceBefore = backend.currentIni;

  struct InvalidCase {
    const char *fileName;
    const char *json;
    const char *expectedError;
  };
  const InvalidCase cases[] = {
      {"format.layout.json",
          R"({"format":"other","version":1,"name":"Bad","layout_ini":"ini"})",
          "format"},
      {"version.layout.json",
          R"({"format":"jsb-editor-layout","version":999,"name":"Bad","layout_ini":"ini"})",
          "version"},
      {"malformed.layout.json", "{ this is not JSON", "JSON"},
      {"missing.layout.json",
          R"({"format":"jsb-editor-layout","version":1,"name":"Bad"})",
          "Missing"},
      {"empty.layout.json",
          R"({"format":"jsb-editor-layout","version":1,"name":"Bad","layout_ini":""})",
          "empty"},
  };

  for (const InvalidCase &invalid : cases) {
    const std::filesystem::path path = temporary.Get() / invalid.fileName;
    passed = Expect(WriteText(path, invalid.json), "write invalid fixture")
             && passed;
    gui::LayoutPresetId importedId = "unchanged";
    passed = Expect(!manager.ImportPreset(path, &importedId),
                 "invalid import is rejected")
             && passed;
    passed = Expect(manager.GetLastError().find(invalid.expectedError)
                        != std::string::npos,
                 "invalid import reports a concise validation error")
             && passed;
    passed =
        Expect(importedId == "unchanged", "failed import does not assign an ID")
        && passed;
    passed = Expect(manager.GetPresets().size() == originalCount
                        && manager.GetPresets().front().id == originalId,
                 "failed import does not mutate preset list")
             && passed;
    passed =
        Expect(backend.currentIni == workspaceBefore && backend.applyCount == 0,
            "failed import does not alter current workspace")
        && passed;
  }
  return passed;
}

bool TestPortableSchemaAndSuggestedName() {
  gui::EditorLayoutExportData parsed;
  std::string serialized;
  std::string error;
  bool passed =
      Expect(gui::EditorLayoutFileSerializer::Serialize(
                 gui::EditorLayoutExportData{
                     .name = "Primary / Baseline Compare",
                     .imguiIni = "[Docking][Data]\nDockSpace ID=0x1\n",
                 },
                 serialized,
                 error),
          "portable schema serializes")
      && Expect(serialized.find("\"format\": \"jsb-editor-layout\"")
                    != std::string::npos,
          "schema includes format")
      && Expect(serialized.find("\"version\": 1") != std::string::npos,
          "schema includes version")
      && Expect(gui::EditorLayoutFileSerializer::Deserialize(serialized,
                    parsed,
                    error),
          "pretty JSON deserializes")
      && Expect(parsed.name == "Primary / Baseline Compare",
          "schema preserves display name")
      && Expect(
          gui::EditorLayoutManager::MakeSuggestedExportFileName(parsed.name)
              == "primary-baseline-compare.layout.json",
          "suggested filename is sanitized independently");
  return passed;
}

bool TestWindowVisibilityRoundTrip() {
  ImGui::CreateContext();
  TestWindow primary("Primary", "PrimaryStable");
  TestWindow baseline("Baseline", "BaselineStable");
  std::vector<gui::Window *> windows{&primary, &baseline};
  gui::EditorWindowStateSettings windowSettings;
  bool passed = Expect(windowSettings.Register(windows),
      "window visibility settings handler registers");

  primary.SetVisible(false);
  baseline.SetVisible(true);
  std::size_t snapshotSize = 0;
  const char *snapshotData = ImGui::SaveIniSettingsToMemory(&snapshotSize);
  const std::string snapshot(snapshotData, snapshotSize);
  passed =
      Expect(snapshot.find("[JSBWindow][PrimaryStable]") != std::string::npos
                 && snapshot.find("Visible=0") != std::string::npos,
          "visibility is part of the ImGui layout snapshot")
      && passed;

  primary.SetVisible(true);
  baseline.SetVisible(false);
  ImGui::LoadIniSettingsFromMemory(snapshot.data(), snapshot.size());
  passed = Expect(!primary.IsVisible() && baseline.IsVisible(),
               "saved visibility restores by stable window ID")
           && passed;

  TemporaryDirectory temporary;
  gui::ImGuiEditorLayoutBackend backend;
  gui::EditorLayoutManager manager(temporary.Get() / "editor", &backend);
  gui::LayoutPresetId presetId;
  passed =
      Expect(manager.Initialize(), "visibility manager initializes") && passed;
  passed = Expect(manager.SaveCurrentLayout("Visibility", &presetId),
               "manager captures window visibility")
           && passed;
  primary.SetVisible(true);
  baseline.SetVisible(false);
  passed = Expect(manager.ApplyPreset(presetId),
               "manager applies complete presentation state")
           && passed;
  passed = Expect(!primary.IsVisible() && baseline.IsVisible(),
               "manager preset restores window visibility")
           && passed;

  ImGui::LoadIniSettingsFromMemory("[Window][Legacy]\nPos=1,2\n");
  passed = Expect(primary.IsVisible() && baseline.IsVisible(),
               "legacy presets without visibility use safe visible defaults")
           && passed;
  ImGui::DestroyContext();
  return passed;
}
} // namespace

int main() {
  bool passed = true;
  passed = TestExportRoundTripAndNoMutation() && passed;
  passed = TestPresetLifecycle() && passed;
  passed = TestDuplicateImportIdsNamesAndOrdering() && passed;
  passed = TestInvalidImportsAreTransactional() && passed;
  passed = TestPortableSchemaAndSuggestedName() && passed;
  passed = TestWindowVisibilityRoundTrip() && passed;
  if (!passed) {
    return 1;
  }
  std::cout << "Editor layout manager tests passed\n";
  return 0;
}
