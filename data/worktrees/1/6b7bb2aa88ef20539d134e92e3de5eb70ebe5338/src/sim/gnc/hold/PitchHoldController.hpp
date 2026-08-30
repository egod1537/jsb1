#pragma once

#include "sim/gnc/Controller.hpp"

#include <optional>

namespace sim {
class Aircraft;
struct Tick;
} // namespace sim

namespace gnc {
struct ControlContext;

struct PitchHoldSettings {
  double targetPitchRad{};
  double dampingRatio{};
  double naturalFrequencyRadPerSec{};
};

class PitchHoldController final : public Controller {
public:
  void Reset() override;

  bool IsEnabled() const;
  void SetEnabled(bool enabled);

  const PitchHoldSettings &GetSettings() const;
  void SetSettings(const PitchHoldSettings &settings);

  double GetTrimElevator() const;
  void SetTrimElevator(double trimElevator);

  std::optional<double> OnTick(const sim::Aircraft &aircraft,
      const sim::Tick &tick, const ControlContext &context);

private:
  bool enabled_ = false;
  PitchHoldSettings settings_;
  double trimElevator_ = 0.0;
};
} // namespace gnc
