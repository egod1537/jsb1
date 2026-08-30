#pragma once

#include "gui/Window.hpp"
#include "gui/features/flightviz/FlightVisualizer.hpp"
#include "sim/runtime/SimulationContracts.hpp"

namespace gui {
class EditorIconRegistry;

class FlightVizWindow final : public gui::Window {
public:
  explicit FlightVizWindow(sim::SimulationSlot slot,
      EditorIconRegistry *icons = nullptr);

  sim::SimulationSlot GetSimulationSlot() const { return slot_; }
  viz::FlightVisualizer &GetVisualizer() { return visualizer_; }
  const viz::FlightVisualizer &GetVisualizer() const { return visualizer_; }

protected:
  ImGuiWindowFlags GetWindowFlags() const override;
  void OnRender(const sim::SimulationSnapshot &snapshot) override;

private:
  void Handle(const FlightVizShadowVisibilityChanged &event);
  void Handle(const FlightVizCameraViewToggleRequested &event);
  void Handle(const FlightVizDisplayOptionsChanged &event);
  void Handle(const FlightVizClearPathRequested &event);

  sim::SimulationSlot slot_;
  EditorIconRegistry *icons_ = nullptr;
  viz::FlightVisualizer visualizer_;
};
} // namespace gui
