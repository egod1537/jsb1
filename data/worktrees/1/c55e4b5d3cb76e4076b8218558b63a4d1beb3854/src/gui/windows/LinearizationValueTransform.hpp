#pragma once

namespace gui {
enum class LinearizationValueTransform {
  Raw,
  SignedLog10,
};

double TransformLinearizationValue(double value,
    LinearizationValueTransform transform);
} // namespace gui
