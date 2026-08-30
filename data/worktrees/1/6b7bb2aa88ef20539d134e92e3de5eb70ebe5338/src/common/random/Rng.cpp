#include "Rng.hpp"
#include <cstdint>
#include <random>

namespace util {

Rng::Rng(std::uint32_t seed) : engine_(seed) {}

void Rng::Seed(std::uint32_t seed) { engine_.seed(seed); }

double Rng::Uniform(double l, double r) {
  return std::uniform_real_distribution<double>(l, r)(engine_);
}

int Rng::UniformInt(int l, int r) {
  return std::uniform_int_distribution<int>(l, r)(engine_);
}

double Rng::Normal(double mean, double stdDev) {
  return std::normal_distribution<double>(mean, stdDev)(engine_);
}

bool Rng::Bernoulli(double prob) {
  return std::bernoulli_distribution(prob)(engine_);
}

} // namespace util
