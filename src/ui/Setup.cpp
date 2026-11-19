#include "Setup.hpp"

#include <gnuradio-4.0/GrAudioBlocks.hpp>

#include <imgui.h>
#include <implot.h>
#include <implot3d.h>

#include "LogHistory.hpp"
#include "blocks/GainEnvelope.hpp"
#include "blocks/ImControlCamera.hpp"
#include "blocks/ImControlChoice.hpp"
#include "blocks/ImControlColor.hpp"
#include "blocks/ImControlDevice.hpp"
#include "blocks/ImControlNumber.hpp"
#include "blocks/ImControlText.hpp"
#include "blocks/ImControlToggle.hpp"
#include "blocks/ImControlTrigger.hpp"
#include "blocks/ImControlValue.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/ImPropertyReadout.hpp"
#include "blocks/LowPassFilter.hpp"
#include "blocks/PianoControl.hpp"
#include "blocks/SineOscillator.hpp"
#include "blocks/StatusBarBlock.hpp"
#include "blocks/TagSink.hpp"
#include "blocks/ValueSource.hpp"
#include "charts/Charts.hpp"
#include "components/ImGuiNotify.hpp"
#include "components/SignalColours.hpp"

namespace DigitizerUi {

void registerDashboardBlocks(gr::BlockRegistry& registry) {
    using namespace opendigitizer::charts;
    registerChart<XYChart>(registry);
    registerChart<YYChart>(registry);
    registerChart<SpectrumPlot>(registry);
    registerChart<SpectrumView>(registry);
    registerChart<SpectrumDensity>(registry);
    registerChart<WaterfallPlot>(registry);
    registerChart<SurfacePlot>(registry);
    registerChart<ImageChart>(registry);
    registerChart<TensorRowsChart>(registry);
    registerChart<LocalisationChart>(registry);
    registerVideoBlocks(registry);
    registerLocalisationBlocks(registry);
    opendigitizer::registerPlotSinks<float, double, gr::DataSet<float>, gr::DataSet<double>, gr::UncertainValue<float>, gr::Tensor<float>, gr::Tensor<double>, gr::Tensor<std::uint32_t>, gr::property_map>(registry);
    std::ignore = gr::registerBlock<opendigitizer::TagSink, std::uint8_t, float>(registry);
    std::ignore = gr::registerBlock<ImControlNumber>(registry);
    std::ignore = gr::registerBlock<ImControlToggle>(registry);
    std::ignore = gr::registerBlock<ImControlTrigger>(registry);
    std::ignore = gr::registerBlock<ImControlText>(registry);
    std::ignore = gr::registerBlock<opendigitizer::ImControlValue, float, double>(registry);
    std::ignore = gr::registerBlock<opendigitizer::ValueSource, float, double>(registry);
    std::ignore = gr::registerBlock<ImControlCamera>(registry);
    std::ignore = gr::registerBlock<ImControlDevice>(registry);
    std::ignore = gr::registerBlock<ImControlChoice>(registry);
    std::ignore = gr::registerBlock<ImControlColor>(registry);
    std::ignore = gr::registerBlock<ImPropertyReadout>(registry);
    std::ignore = gr::registerBlock<SchedulerStateIndicator>(registry);
    std::ignore = gr::registerBlock<opendigitizer::GainEnvelope, float, int16_t>(registry);
    std::ignore = gr::registerBlock<opendigitizer::LowPassFilter, float>(registry);
    std::ignore = gr::registerBlock<opendigitizer::PianoControl>(registry);
    std::ignore = gr::registerBlock<opendigitizer::SineOscillator, float>(registry);
    registerSignalBlocks(registry);
    gr::blocklib::initGrAudioBlocks(registry);
}

void applyStyle(LookAndFeel::Style style) {
    LookAndFeel::mutableInstance().style = style;
    LookAndFeel::mutableInstance().applyTheme();
}

void applyTheme(LookAndFeel::Theme theme) {
    LookAndFeel::mutableInstance().theme = theme;
    LookAndFeel::mutableInstance().applyTheme();
}

void initialise(std::optional<LookAndFeel::Style> style) {
    assert(ImGui::GetCurrentContext() != nullptr && "initialise() needs the host's ImGui context");
    if (ImPlot::GetCurrentContext() == nullptr) {
        ImPlot::CreateContext();
    }
    if (ImPlot3D::GetCurrentContext() == nullptr) {
        ImPlot3D::CreateContext();
    }
    if (LookAndFeel::instance().fontNormal() == nullptr) {
        LookAndFeel::mutableInstance().loadFonts();
    }
    std::ignore = opendigitizer::SignalColours::instance();
    registerDashboardBlocks(gr::globalBlockRegistry());
    std::ignore = logHistory();
    if (style) {
        applyStyle(*style);
    }
}

} // namespace DigitizerUi
