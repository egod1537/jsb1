#include "gui/resources/EditorIconRegistry.hpp"

#include <cassert>

int main() {
  gui::EditorIconRegistry icons;
  assert(icons.Initialize());
  assert(icons.GetIndex().size() == 2205);
  assert(icons.GetLoadedCount() == 0);

  assert(icons.Contains("GameObject Icon"));
  assert(icons.Contains("SceneAsset Icon.png"));
  assert(icons.Contains("d_console.infoicon"));
  assert(icons.Contains("icons/small/d_PlayButton.png"));
  assert(icons.Contains(gui::EditorIconAliases::ShadowAircraft));
  assert(icons.Contains(gui::EditorIconAliases::ViewOptions));
  assert(icons.Contains(gui::EditorIconAliases::CameraView));
  assert(icons.Contains("processed/unityengine/d_TextAsset Icon"));

  // Duplicate stems require their unambiguous relative name.
  assert(!icons.Contains("Warning"));
  assert(icons.Contains("packagemanager/dark/Warning"));
  assert(icons.Contains("packagemanager/light/Warning"));
  assert(!icons.Contains("Missing Editor Icon"));

  icons.Shutdown();
  return 0;
}
