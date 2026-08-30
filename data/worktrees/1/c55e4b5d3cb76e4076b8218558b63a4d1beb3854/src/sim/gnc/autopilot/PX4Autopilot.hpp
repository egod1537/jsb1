#pragma once

#include "sim/gnc/autopilot/IAutopilot.hpp"
#include "sim/gnc/autopilot/IControllerInspectable.hpp"
#include "sim/gnc/autopilot/IRollHoldAutopilot.hpp"
#include "sim/gnc/autopilot/ITrimReferenceConsumer.hpp"
#include "sim/gnc/hold/Px4RollHoldReferenceController.hpp"

namespace gnc {
class PX4Autopilot final : public IAutopilot,
                           public IControllerInspectable,
                           public IRollHoldAutopilot,
                           public ITrimReferenceConsumer {
public:
  // Lifecycle and control output
  void Reset() override;
  control::ControlInput Update(sim::Aircraft &aircraft, const sim::Tick &tick,
      const control::ControlInput &passthroughCommand) override;

  // Baseline Roll Hold
  bool IsRollHoldEnabled() const override;
  void SetRollHoldEnabled(bool enabled) override;
  double GetTargetRollRad() const override;
  void SetTargetRollRad(double targetRollRad) override;

  // PX4 reference settings and diagnostics
  const Px4RollHoldReferenceSettings &GetRollHoldSettings() const;
  void SetRollHoldSettings(const Px4RollHoldReferenceSettings &settings);
  const Px4RollHoldReferenceDiagnostics &GetRollHoldDiagnostics() const;

  // Trim reference consumption
  void SynchronizeTrimReferences(sim::Aircraft &aircraft,
      const TrimResult &trimResult) override;

private:
  // Controller lookup
  Controller *FindController(const std::type_info &type) override;
  const Controller *FindController(const std::type_info &type) const override;

  // Roll control
  Px4RollHoldReferenceController rollHoldController_;
  double targetRollRad_ = 0.0;
};
} // namespace gnc
