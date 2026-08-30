#pragma once

namespace gnc {
class IRollHoldAutopilot {
public:
  virtual ~IRollHoldAutopilot() = default;

  // Roll Hold execution contract
  virtual bool IsRollHoldEnabled() const = 0;
  virtual void SetRollHoldEnabled(bool enabled) = 0;
  virtual double GetTargetRollRad() const = 0;
  virtual void SetTargetRollRad(double targetRollRad) = 0;
};
} // namespace gnc
