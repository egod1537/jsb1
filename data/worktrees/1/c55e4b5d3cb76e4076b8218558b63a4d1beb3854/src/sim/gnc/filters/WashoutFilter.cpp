#include "WashoutFilter.hpp"

namespace util {
void WashoutFilter::Reset(double input) {
  prevInput_ = input;
  prevOutput_ = 0.0;
}

double WashoutFilter::Update(double input, double poleRadPerSec, double dt) {
  const double denom = 2.0 + poleRadPerSec * dt;

  const double a = (2.0 - poleRadPerSec * dt) / denom;
  const double b = 2.0 / denom;

  const double output = a * prevOutput_ + b * (input - prevInput_);

  prevInput_ = input;
  prevOutput_ = output;

  return output;
}
} // namespace util
