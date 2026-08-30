#include "gui/features/linearization/LinearizationController.hpp"

#include "messaging/SimulationMessageClient.hpp"

namespace gui {
LinearizationController::LinearizationController(
    application::SimulationMessageClient &client)
    : client_(client) {}

void LinearizationController::Handle(
    const AutomaticLinearizationChanged &event) {
  client_.SetAutomaticLinearizationEnabled(event.enabled);
}

void LinearizationController::Handle(
    const LinearizationValueTransformChanged &event) {
  model_.valueTransform = event.transform;
}
} // namespace gui
