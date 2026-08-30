#pragma once

#include <cstdint>
#include <random>
namespace util {
class Rng {
public:
  explicit Rng(std::uint32_t seed = std::random_device{}());

  void Seed(std::uint32_t seed);

  double Uniform(double l, double r);
  int UniformInt(int l, int r);
  double Normal(double mean, double stdDev);
  bool Bernoulli(double prob);

private:
  std::mt19937 engine_;
};
} // namespace util
