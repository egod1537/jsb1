#include "flightui/controls/Latex.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/latex/LatexRenderer.hpp"

#include <utility>

namespace FlightUI {
UIElement Latex(std::string source, LatexOptions options) {
  return CreateElement(
      [source = std::move(source), options] {
        Internal::GetLatexRenderer().Render(source, options);
      });
}
} // namespace FlightUI
