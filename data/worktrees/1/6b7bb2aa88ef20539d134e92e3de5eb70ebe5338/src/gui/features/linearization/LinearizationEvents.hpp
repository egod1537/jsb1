#pragma once

#include "gui/windows/LinearizationValueTransform.hpp"

namespace gui {
struct AutomaticLinearizationChanged {
  bool enabled = false;
};

struct LinearizationValueTransformChanged {
  LinearizationValueTransform transform = LinearizationValueTransform::Raw;
};
} // namespace gui
