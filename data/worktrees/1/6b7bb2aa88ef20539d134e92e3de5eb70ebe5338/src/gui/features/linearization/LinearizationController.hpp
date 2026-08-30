#pragma once

#include "gui/features/linearization/LinearizationEvents.hpp"

namespace application {
class SimulationMessageClient;
}

namespace gui {
struct LinearizationModel {
  LinearizationValueTransform valueTransform = LinearizationValueTransform::Raw;
};

class LinearizationController {
public:
  explicit LinearizationController(
      application::SimulationMessageClient &client);

  const LinearizationModel &GetModel() const { return model_; }
  void Handle(const AutomaticLinearizationChanged &event);
  void Handle(const LinearizationValueTransformChanged &event);

private:
  application::SimulationMessageClient &client_;
  LinearizationModel model_;
};
} // namespace gui
