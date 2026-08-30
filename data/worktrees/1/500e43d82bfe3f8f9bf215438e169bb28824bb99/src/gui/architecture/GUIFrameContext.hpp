#pragma once

namespace sim {
struct SimulationSnapshot;
}

namespace gui {
class EditorIconRegistry;

// Immutable application state plus render-only resources for the legacy frame
// shell. Application services are intentionally excluded; feature controllers
// receive those explicitly at construction.
struct GUIFrameContext {
  const sim::SimulationSnapshot &simulation;
  EditorIconRegistry &icons;
};
} // namespace gui
