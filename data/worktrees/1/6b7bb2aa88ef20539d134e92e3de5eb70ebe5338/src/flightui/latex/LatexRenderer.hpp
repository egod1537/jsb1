#pragma once

#include "flightui/controls/Latex.hpp"

#include <memory>
#include <string>

namespace FlightUI {
class LatexRenderer {
public:
  virtual ~LatexRenderer() = default;

  virtual void Render(
      const std::string &source, const LatexOptions &options) = 0;
};

namespace Internal {
void SetLatexRenderer(std::unique_ptr<LatexRenderer> renderer);
LatexRenderer &GetLatexRenderer();
} // namespace Internal
} // namespace FlightUI
