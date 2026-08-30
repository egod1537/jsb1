#pragma once

#include "sim/Component.hpp"

namespace sim {
class StateLogger final : public Component {
protected:
  bool OnInitialize() override;
  bool OnReset() override;
  bool OnPostTick(const Tick &tick) override;

private:
  void ResetLogTimer();
  void PrintState() const;

  double nextLogTime_ = 0.0;
};
} // namespace sim
