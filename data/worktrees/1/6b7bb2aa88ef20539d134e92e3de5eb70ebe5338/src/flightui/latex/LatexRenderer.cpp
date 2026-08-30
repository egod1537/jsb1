#include "flightui/latex/LatexRenderer.hpp"

#include <imgui.h>

#include <memory>
#include <utility>

namespace {
class UnavailableLatexRenderer final : public FlightUI::LatexRenderer {
public:
  void Render(const std::string &, const FlightUI::LatexOptions &) override {
    ImGui::TextDisabled("[LaTeX renderer unavailable]");
  }
};

std::unique_ptr<FlightUI::LatexRenderer> &RendererSlot() {
  static std::unique_ptr<FlightUI::LatexRenderer> renderer;
  return renderer;
}
} // namespace

namespace FlightUI::Internal {
void SetLatexRenderer(std::unique_ptr<LatexRenderer> renderer) {
  RendererSlot() = std::move(renderer);
}

LatexRenderer &GetLatexRenderer() {
  if (RendererSlot()) {
    return *RendererSlot();
  }

  static UnavailableLatexRenderer unavailableRenderer;
  return unavailableRenderer;
}
} // namespace FlightUI::Internal
