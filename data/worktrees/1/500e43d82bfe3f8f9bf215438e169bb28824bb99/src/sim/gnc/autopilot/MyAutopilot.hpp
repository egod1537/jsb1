#pragma once

#include "sim/Aircraft.hpp"
#include "sim/Tick.hpp"
#include "sim/gnc/autopilot/IAutopilot.hpp"
#include "sim/gnc/autopilot/IAutopilotAnalysis.hpp"
#include "sim/gnc/autopilot/IControllerInspectable.hpp"
#include "sim/gnc/autopilot/IRollHoldAutopilot.hpp"
#include "sim/gnc/autopilot/ITrimReferenceConsumer.hpp"
#include "sim/gnc/Controller.hpp"
#include "sim/gnc/hold/PitchDynamics.hpp"
#include "sim/gnc/hold/RollDynamics.hpp"
#include "sim/gnc/hold/RollHoldController.hpp"
#include "sim/gnc/hold/YawDynamics.hpp"
#include "sim/gnc/TrimTypes.hpp"
#include "sim/linearization/DynamicModeAnalyzer.hpp"
#include "sim/linearization/DynamicModeHistory.hpp"
#include "sim/linearization/LinearizationResult.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace sim {
class AsyncAircraftLinearizer;
}

namespace gnc {
class MyAutopilot final : public IAutopilot,
                          public IAutopilotAnalysis,
                          public IControllerInspectable,
                          public IRollHoldAutopilot,
                          public ITrimReferenceConsumer {
public:
  MyAutopilot();
  ~MyAutopilot() override;

  // Lifecycle and control output
  void Reset() override;
  control::ControlInput Update(sim::Aircraft &aircraft, const sim::Tick &tick,
      const control::ControlInput &passthroughCommand) override;

  // Periodic aircraft dynamics
  void UpdateLinearization(sim::Aircraft &aircraft,
      const sim::Tick &tick) override;
  bool IsAutomaticLinearizationEnabled() const override;
  void SetAutomaticLinearizationEnabled(bool enabled) override;

  // Controller registry
  template <typename T, typename... Args> T *AddController(Args &&...args);
  template <typename T> bool RemoveController();

  // Trim reference consumption
  void SynchronizeTrimReferences(sim::Aircraft &aircraft,
      const TrimResult &trimResult) override;

  // Roll Hold state
  bool IsRollHoldEnabled() const override;
  void SetRollHoldEnabled(bool enabled) override;
  double GetTargetRollRad() const override;
  void SetTargetRollRad(double targetRollRad) override;

  // Roll Hold settings
  void SetRollHoldSettings(const RollHoldSettings &settings);
  const RollHoldSettings &GetRollHoldSettings() const;

  // Linearization
  bool IsLinearizationInProgress() const override;
  const LinearizationResult *GetLinearizationResult() const override;
  const DynamicModeAnalysis *GetDynamicModeAnalysis() const override;
  const DynamicModeHistory &GetDynamicModeHistory() const override;
  std::string_view GetLinearizationErrorMessage() const override;
  std::optional<RollDynamics> GetRollDynamics() const override;
  std::optional<PitchDynamics> GetPitchDynamics() const override;
  std::optional<YawDynamics> GetYawDynamics() const override;

private:
  // Interface controller lookup
  Controller *FindController(const std::type_info &type) override;
  const Controller *FindController(const std::type_info &type) const override;

  // Controller trim synchronization
  void ResetControllers();
  void SyncControllerTrimReferences(const TrimResult &result);

  // Aircraft dynamics
  void PollLinearization();
  bool SubmitLinearization(sim::Aircraft &aircraft, double simulationTimeSec);
  void InvalidateLinearization();

  // Controller ownership
  std::vector<std::unique_ptr<Controller>> controllers_;

  // Aircraft dynamics
  std::unique_ptr<sim::AsyncAircraftLinearizer> asyncLinearizer_;
  std::optional<LinearizationResult> linearization_;
  DynamicModeHistory dynamicModeHistory_;
  std::string linearizationErrorMessage_;
  std::optional<double> lastLinearizationCycleSimTimeSec_;
  std::uint64_t linearizationGeneration_ = 0;
  bool automaticLinearizationEnabled_ = true;
};

} // namespace gnc
#include "sim/gnc/autopilot/MyAutopilot.inl"
