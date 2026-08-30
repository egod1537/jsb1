#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace math {
constexpr double DegToRad(double degrees) {
  return degrees * std::numbers::pi_v<double> / 180.0;
}

constexpr double RadToDeg(double radians) {
  return radians * 180.0 / std::numbers::pi_v<double>;
}

inline double WrapAngleRad(double angle) {
  return std::remainder(angle, 2.0 * std::numbers::pi_v<double>);
}

inline double WrapAngleDeg(double angle) {
  return std::remainder(angle, 360.0);
}

inline double Wrap(double value, double min, double max) {
  if (!std::isfinite(min) || !std::isfinite(max) || !(min < max)) {
    throw std::invalid_argument("Wrap requires a finite range with min < max");
  }

  const double period = max - min;
  if (!std::isfinite(period)) {
    throw std::invalid_argument("Wrap requires a finite range width");
  }

  double wrapped = std::fmod(value - min, period);
  if (wrapped < 0.0) {
    wrapped += period;
  }
  if (wrapped >= period) {
    wrapped = 0.0;
  }

  return min + wrapped;
}

constexpr double Lerp(double a, double b, double t) { return a + (b - a) * t; }

inline double InverseLerp(double a, double b, double value) {
  if (a == b) {
    return 0.0;
  }

  return (value - a) / (b - a);
}

inline double MoveTowards(double current, double target, double maxDelta) {
  if (maxDelta < 0.0) {
    throw std::invalid_argument("MoveTowards requires maxDelta >= 0");
  }

  const double delta = target - current;
  if (std::abs(delta) <= maxDelta) {
    return target;
  }

  return current + std::copysign(maxDelta, delta);
}

inline bool Approximately(double a, double b, double epsilon = 1.0e-9) {
  if (!(epsilon >= 0.0)) {
    return false;
  }
  if (a == b) {
    return true;
  }
  if (!std::isfinite(a) || !std::isfinite(b)) {
    return false;
  }

  const double difference = std::abs(a - b);
  const double scale = std::max({1.0, std::abs(a), std::abs(b)});
  return difference <= epsilon * scale;
}

inline double DeltaAngleRad(double from, double to) {
  return WrapAngleRad(to - from);
}

inline double DeltaAngleDeg(double from, double to) {
  return WrapAngleDeg(to - from);
}
} // namespace math
