#include "DashboardBlocks.hpp"

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
#include "blocks/SchedulerStateIndicator.hpp"
#include "blocks/SineOscillator.hpp"
#include "blocks/TagSink.hpp"
#include "blocks/ValueSource.hpp"
#include "charts/Charts.hpp"
#include "components/ImGuiNotify.hpp"
#include "components/SignalColours.hpp"

namespace gr::od {

void registerDashboardBlocks(gr::BlockRegistry& registry) {
    using namespace charts;
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
    registerPlotSinks<float, double, gr::DataSet<float>, gr::DataSet<double>, gr::UncertainValue<float>, gr::Tensor<float>, gr::Tensor<double>, gr::Tensor<std::uint32_t>, gr::property_map>(registry);
    registerWithIdPrefix<TagSink, std::uint8_t, float>(registry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<ImControlNumber>(registry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImControlToggle>(registry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImControlTrigger>(registry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImControlText>(registry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImControlValue, float, double>(registry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<ValueSource, float, double>(registry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<ImControlCamera>(registry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImControlDevice>(registry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImControlChoice>(registry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImControlColor>(registry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImPropertyReadout>(registry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<SchedulerStateIndicator>(registry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<GainEnvelope, float, int16_t>(registry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<LowPassFilter, float>(registry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<PianoControl>(registry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<SineOscillator, float>(registry, kOpendigitizerIdPrefix);
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
    std::ignore = SignalColours::instance();
    registerDashboardBlocks(gr::globalBlockRegistry());
    std::ignore = logHistory();
    if (style) {
        applyStyle(*style);
    }
}

} // namespace gr::od
