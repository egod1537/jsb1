#include "gui/windows/samples/SamplePlotWindow.hpp"
#include "flightui/FlightUI.hpp"
#include <array>
#include <cmath>

namespace gui {
namespace UI = FlightUI;

SamplePlotWindow::SamplePlotWindow() : Window("ImPlot Test") {}

void SamplePlotWindow::OnRender(const sim::SimulationSnapshot &) {
  constexpr int PointCount = 240;

  std::array<double, PointCount> xs{};
  std::array<double, PointCount> ys{};

  const double time = UI::GetTime();
  for (int index = 0; index < PointCount; ++index) {
    xs[index] = static_cast<double>(index) / 24.0;
    ys[index] = std::sin(xs[index] + time);
  }

  UI::UIElement plot = UI::Plot("Sample Signal")
                           .Height(300.0F)
                           .XAxisLabel("Time")
                           .YAxisLabel("Value")
                           .AddLine("sin(t)",
                               FlightUI::DataView(xs.data(), xs.size()),
                               FlightUI::DataView(ys.data(), ys.size()));

  plot.Render();
}
} // namespace gui
