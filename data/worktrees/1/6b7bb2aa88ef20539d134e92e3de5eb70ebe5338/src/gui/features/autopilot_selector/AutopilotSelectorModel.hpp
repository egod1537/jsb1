#pragma once

namespace gui {
enum class AutopilotSelection {
  Primary,
  Baseline,
};

// Local feature state. Autopilot telemetry remains authoritative in snapshots.
class AutopilotViewState {
public:
  AutopilotSelection GetSelection() const { return selection_; }

  bool Select(AutopilotSelection selection, bool baselineAvailable) {
    if (selection == AutopilotSelection::Baseline && !baselineAvailable) {
      return false;
    }
    selection_ = selection;
    return true;
  }

  void EnsureBaselineAvailable(bool baselineAvailable) {
    if (!baselineAvailable && selection_ == AutopilotSelection::Baseline) {
      selection_ = AutopilotSelection::Primary;
    }
  }

private:
  AutopilotSelection selection_ = AutopilotSelection::Primary;
};
} // namespace gui
