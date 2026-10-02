#include "Setup.hpp"

#include <imgui.h>
#include <implot.h>
#include <implot3d.h>

#include "blocks/ImPlotSink.hpp"
#include "charts/Charts.hpp"
#include "components/ColourManager.hpp"

namespace DigitizerUi {

void registerDashboardBlocks(gr::BlockRegistry& registry) {
    using namespace opendigitizer::charts;
    std::ignore = gr::registerBlock<XYChart>(registry);
    std::ignore = gr::registerBlock<YYChart>(registry);
    std::ignore = gr::registerBlock<SpectrumPlot>(registry);
    std::ignore = gr::registerBlock<SpectrumView>(registry);
    std::ignore = gr::registerBlock<SpectrumDensity>(registry);
    std::ignore = gr::registerBlock<WaterfallPlot>(registry);
    std::ignore = gr::registerBlock<SurfacePlot>(registry);
    std::ignore = gr::registerBlock<opendigitizer::ImPlotSink, float, gr::DataSet<float>, gr::UncertainValue<float>>(registry);
}

void applyStyle(LookAndFeel::Style style) {
    switch (style) {
    case LookAndFeel::Style::Dark: ImGui::StyleColorsDark(); break;
    case LookAndFeel::Style::Light: ImGui::StyleColorsLight(); break;
    }
    LookAndFeel::mutableInstance().style = style;

    ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 1.f;

    // with the dark style the plot frame would have the same colour as a button; give it the window background instead
    ImPlot::GetStyle().Colors[ImPlotCol_FrameBg] = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
}

void initialise(const InitialiseOptions& options) {
    assert(ImGui::GetCurrentContext() != nullptr && "initialise() needs the host's ImGui context");
    if (ImPlot::GetCurrentContext() == nullptr) {
        ImPlot::CreateContext();
    }
    if (ImPlot3D::GetCurrentContext() == nullptr) {
        ImPlot3D::CreateContext();
    }
    if (options.loadFonts && LookAndFeel::instance().fontNormal[0] == nullptr) {
        LookAndFeel::mutableInstance().loadFonts();
    }
    std::ignore = opendigitizer::ColourManager::instance();
    registerDashboardBlocks(options.registry ? *options.registry : gr::globalBlockRegistry());
    if (options.style) {
        applyStyle(*options.style);
    }
}

} // namespace DigitizerUi
