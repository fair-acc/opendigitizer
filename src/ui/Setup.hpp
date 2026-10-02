#ifndef OPENDIGITIZER_UI_SETUP_HPP
#define OPENDIGITIZER_UI_SETUP_HPP

#include <optional>

#include <gnuradio-4.0/BlockRegistry.hpp>

#include "common/LookAndFeel.hpp"

namespace DigitizerUi {

/// One-time setup for drawing dashboards, by the OpenDigitizer App or by a host application that embeds them.
/// Call once after the ImGui context exists and before the first Dashboard is created:
/// @code
/// ImGui::CreateContext();
/// DigitizerUi::initialise();                                         // keeps the host's ImGui style
/// DigitizerUi::initialise({.style = DigitizerUi::LookAndFeel::Style::Dark}); // or: OpenDigitizer's look
/// @endcode
struct InitialiseOptions {
    gr::BlockRegistry*                registry   = nullptr; // nullptr: gr::globalBlockRegistry()
    std::optional<LookAndFeel::Style> style      = {};      // empty: the host's ImGui/ImPlot style stays untouched
    bool                              loadFonts  = true;
    bool                              captureLog = true; // GR4 log records and notifications are kept in logHistory() for the status bar
};

void initialise(const InitialiseOptions& options = {});

/// the chart types and the plot sink a dashboard needs, independent of which headers the caller includes
void registerDashboardBlocks(gr::BlockRegistry& registry);

/// OpenDigitizer's ImGui and ImPlot colours for the given style
void applyStyle(LookAndFeel::Style style);

} // namespace DigitizerUi

#endif // OPENDIGITIZER_UI_SETUP_HPP
