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
#include "blocks/ImPropertyReadout.hpp"
#include "blocks/LowPassFilter.hpp"
#include "blocks/PianoControl.hpp"
#include "blocks/SchedulerStateIndicator.hpp"
#include "blocks/SineOscillator.hpp"
#include "blocks/ValueSource.hpp"
#include "charts/Charts.hpp"
#include "components/ImGuiNotify.hpp"
#include "components/SignalColours.hpp"

namespace gr::od {

void registerDashboardBlocks(gr::BlockRegistry& blockRegistry) {
    using namespace charts;
    registerChart<XYChart>(blockRegistry);
    registerChart<YYChart>(blockRegistry);
    registerChart<SpectrumPlot>(blockRegistry);
    registerChart<SpectrumView>(blockRegistry);
    registerChart<SpectrumDensity>(blockRegistry);
    registerChart<WaterfallPlot>(blockRegistry);
    registerChart<SurfacePlot>(blockRegistry);
    registerChart<ImageChart>(blockRegistry);
    registerChart<TensorRowsChart>(blockRegistry);
    registerChart<LocalisationChart>(blockRegistry);
    registerVideoBlocks(blockRegistry);
    registerLocalisationBlocks(blockRegistry);
    registerSinkBlocks(blockRegistry);
    registerFrameSinkBlocks(blockRegistry);
    registerWithIdPrefix<ImControlNumber>(blockRegistry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImControlToggle>(blockRegistry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImControlTrigger>(blockRegistry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImControlText>(blockRegistry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImControlValue, float, double>(blockRegistry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<ValueSource, float, double>(blockRegistry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<ImControlCamera>(blockRegistry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImControlDevice>(blockRegistry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImControlChoice>(blockRegistry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImControlColor>(blockRegistry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<ImPropertyReadout>(blockRegistry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<SchedulerStateIndicator>(blockRegistry, kDigitizerUiIdPrefix);
    registerWithIdPrefix<GainEnvelope, float, int16_t>(blockRegistry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<LowPassFilter, float>(blockRegistry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<PianoControl>(blockRegistry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<SineOscillator, float>(blockRegistry, kOpendigitizerIdPrefix);
    registerSignalBlocks(blockRegistry);
    gr::blocklib::initGrAudioBlocks(blockRegistry);
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
