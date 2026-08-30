#pragma once

#include "gui/Window.hpp"
#include "gui/features/monitor/MonitorView.hpp"

namespace gui {
class MonitorController;

class FlightDataMonitorWindow final : public Window {
public:
  explicit FlightDataMonitorWindow(MonitorController &controller);

protected:
  ImGuiWindowFlags GetWindowFlags() const override;
  void OnRender() override;

private:
  MonitorController &controller_;
  MonitorView view_;
};
} // namespace gui
