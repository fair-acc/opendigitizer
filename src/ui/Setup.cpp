#include "Setup.hpp"

#include <imgui.h>
#include <implot.h>
#include <implot3d.h>

#include "LogHistory.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/StatusBarBlock.hpp"
#include "blocks/ToolbarBlock.hpp"
#include "charts/Charts.hpp"
#include "components/ColourManager.hpp"
#include "components/ImGuiNotify.hpp"

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
    std::ignore = gr::registerBlock<ToolbarButton>(registry);
    std::ignore = gr::registerBlock<ToolbarCheckbox>(registry);
    std::ignore = gr::registerBlock<SchedulerStateIndicator>(registry);
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
    if (LookAndFeel::instance().fontNormal[0] == nullptr) {
        LookAndFeel::mutableInstance().loadFonts();
    }
    std::ignore = opendigitizer::ColourManager::instance();
    registerDashboardBlocks(gr::globalBlockRegistry());
    if (options.captureLog) {
        std::ignore                        = logHistory();
        components::Notification::observer = [](ImGuiToastType type, std::string_view text) {
            const gr::log::Level level = type == ImGuiToastType::Error ? gr::log::Level::error : type == ImGuiToastType::Warning ? gr::log::Level::warning : gr::log::Level::info;
            logHistory().record(level, text);
        };
    }
    if (options.style) {
        applyStyle(*options.style);
    }
}

} // namespace DigitizerUi
