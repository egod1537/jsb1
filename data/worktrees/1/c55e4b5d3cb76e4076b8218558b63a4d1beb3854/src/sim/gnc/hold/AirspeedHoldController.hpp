#pragma once

#include "sim/gnc/Controller.hpp"

#include <optional>

namespace sim {
class Aircraft;
struct Tick;
} // namespace sim

namespace gnc {
struct AirspeedHoldSettings {
  double targetAirspeedMps = 0.0;
  double proportionalGain = 0.5;
  double derivativeGain = 0.0;
};

class AirspeedHoldController final : public Controller {
public:
  void Reset() override;

  bool IsEnabled() const;
  void SetEnabled(bool enabled);

  const AirspeedHoldSettings &GetSettings() const;
  void SetSettings(const AirspeedHoldSettings &settings);

  double GetTrimThrottle() const;
  void SetTrimThrottle(double trimThrottle);

  std::optional<double> OnTick(const sim::Aircraft &aircraft,
      const sim::Tick &tick);

private:
  bool enabled_ = false;
  AirspeedHoldSettings settings_;
  double trimThrottle_ = 0.0;
};
} // namespace gnc
