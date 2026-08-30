#pragma once

#include <algorithm>

namespace control {
enum class ControlAxis {
  Elevator,
  Aileron,
  Rudder,
  Throttle,
};

struct ControlInput {
  double elevator = 0.0;
  double aileron = 0.0;
  double rudder = 0.0;
  double throttle = 0.0;
};

inline double ClampControlAxisValue(ControlAxis axis, double value) {
  if (axis == ControlAxis::Throttle) {
    return std::clamp(value, 0.0, 1.0);
  }

  return std::clamp(value, -1.0, 1.0);
}

inline double GetControlAxisValue(const ControlInput &input, ControlAxis axis) {
  switch (axis) {
  case ControlAxis::Elevator:
    return input.elevator;
  case ControlAxis::Aileron:
    return input.aileron;
  case ControlAxis::Rudder:
    return input.rudder;
  case ControlAxis::Throttle:
    return input.throttle;
  }

  return 0.0;
}

inline bool SetControlAxisValue(ControlInput &input, ControlAxis axis,
    double value) {
  const double clampedValue = ClampControlAxisValue(axis, value);
  double *target = nullptr;

  switch (axis) {
  case ControlAxis::Elevator:
    target = &input.elevator;
    break;
  case ControlAxis::Aileron:
    target = &input.aileron;
    break;
  case ControlAxis::Rudder:
    target = &input.rudder;
    break;
  case ControlAxis::Throttle:
    target = &input.throttle;
    break;
  }

  if (target == nullptr) {
    return false;
  }

  const bool changed = *target != clampedValue;
  *target = clampedValue;
  return changed;
}

inline bool AdjustControlAxisValue(ControlInput &input, ControlAxis axis,
    double delta) {
  return SetControlAxisValue(input,
      axis,
      GetControlAxisValue(input, axis) + delta);
}

inline void ClampControlInput(ControlInput &input) {
  SetControlAxisValue(input, ControlAxis::Elevator, input.elevator);
  SetControlAxisValue(input, ControlAxis::Aileron, input.aileron);
  SetControlAxisValue(input, ControlAxis::Rudder, input.rudder);
  SetControlAxisValue(input, ControlAxis::Throttle, input.throttle);
}

inline bool operator==(const ControlInput &left, const ControlInput &right) {
  return left.elevator == right.elevator && left.aileron == right.aileron
         && left.rudder == right.rudder && left.throttle == right.throttle;
}

inline bool operator!=(const ControlInput &left, const ControlInput &right) {
  return !(left == right);
}
} // namespace control
