#pragma once

namespace sim {
class Aircraft;
} // namespace sim

namespace gnc {
struct TrimResult;

class ITrimReferenceConsumer {
public:
  virtual ~ITrimReferenceConsumer() = default;

  virtual void SynchronizeTrimReferences(sim::Aircraft &aircraft,
      const TrimResult &trimResult) = 0;
};
} // namespace gnc
