#pragma once

#include "gui/features/linearization/LinearizationController.hpp"
#include "gui/Window.hpp"

#include <string_view>

namespace gnc {
struct LinearizationResult;
}

namespace gui {
class LinearizationWindow final : public Window {
public:
  explicit LinearizationWindow(LinearizationController &controller);

protected:
  void OnRender(const sim::SimulationSnapshot &snapshot) override;

private:
  // Display controls
  void DrawTransformSelector();

  // Matrix rendering
  void DrawResult(const gnc::LinearizationResult &result, bool updateInProgress,
      std::string_view errorMessage) const;

  LinearizationController &controller_;
};
} // namespace gui
