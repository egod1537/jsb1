#pragma once

#include "sim/gnc/Controller.hpp"
#include "sim/gnc/filters/WashoutFilter.hpp"

#include <optional>

namespace sim {
class Aircraft;
struct Tick;
} // namespace sim

namespace gnc {
struct ControlContext;

struct YawDamperDiagnostics {
  double filteredRRadPerSec = 0.0;
  double rudderCommand = 0.0;
};

class YawDamperController final : public Controller {
public:
  // Lifecycle
  void Reset() override;

  // Mode
  bool IsEnabled() const;
  void SetEnabled(bool enabled);

  // Trim reference
  double GetTrimRudder() const;
  void SetTrimRudder(double trimRudder);

  // Diagnostics
  const YawDamperDiagnostics &GetDiagnostics() const;

  // Control output
  std::optional<double> OnTick(const sim::Aircraft &aircraft,
      const sim::Tick &tick, const ControlContext &context);

private:
  // Mode
  bool enabled_ = false;

  // Trim reference
  double trimRudder_ = 0.0;

  // Runtime state
  util::WashoutFilter washoutFilter_;

  // Last control result
  YawDamperDiagnostics diagnostics_;
};
} // namespace gnc
