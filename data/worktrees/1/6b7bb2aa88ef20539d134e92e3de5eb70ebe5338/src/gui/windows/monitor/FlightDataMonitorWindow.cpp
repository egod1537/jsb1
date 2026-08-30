#include "gui/windows/monitor/FlightDataMonitorWindow.hpp"

#include "gui/features/monitor/MonitorController.hpp"

#include <imgui.h>

namespace gui {
FlightDataMonitorWindow::FlightDataMonitorWindow(MonitorController &controller)
    : Window("Monitor", EditorIconAliases::Monitor), controller_(controller) {}

ImGuiWindowFlags FlightDataMonitorWindow::GetWindowFlags() const {
  return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void FlightDataMonitorWindow::OnRender() {
  view_.Render(controller_.GetInput(),
      controller_.GetState(),
      architecture::EventSink<MonitorEvent>{
          [this](const MonitorEvent &event) { controller_.Handle(event); }});
}
} // namespace gui
