#pragma once

#include "gui/architecture/GUIFrameContext.hpp"

namespace gui {
class Component {
public:
  // Lifetime
  virtual ~Component() = default;

  Component(const Component &other) = delete;
  Component &operator=(const Component &other) = delete;

  // Enabled state
  bool IsEnabled() const { return enabled_; }
  void SetEnabled(bool enabled) { enabled_ = enabled; }

  // GUI lifecycle entry points
  void StartIfNeeded() {
    if (!enabled_ || started_) {
      return;
    }

    OnStart();
    started_ = true;
  }

  void Tick(const GUIFrameContext &context) {
    if (!enabled_) {
      return;
    }

    StartIfNeeded();
    OnTick(context);
  }

protected:
  Component() = default;

  // Extension hooks
  virtual void OnStart() {}
  virtual void OnTick(const GUIFrameContext &context) = 0;

private:
  // Lifecycle state
  bool enabled_ = true;
  bool started_ = false;
};
} // namespace gui
