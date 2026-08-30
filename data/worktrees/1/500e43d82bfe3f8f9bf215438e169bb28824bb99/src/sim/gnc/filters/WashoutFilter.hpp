#pragma once

namespace util {
struct WashoutFilter {
public:
  void Reset(double input = 0.0);
  double Update(double input, double poleRadPerSec, double dt);

private:
  double prevInput_ = 0.0;
  double prevOutput_ = 0.0;
};
} // namespace util
