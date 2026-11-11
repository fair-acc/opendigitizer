#ifndef OPENDIGITIZER_UI_SETUP_HPP
#define OPENDIGITIZER_UI_SETUP_HPP

#include <optional>

#include <gnuradio-4.0/BlockRegistry.hpp>

#include "common/LookAndFeel.hpp"

namespace DigitizerUi {

void initialise(std::optional<LookAndFeel::Style> style = {}); // once, after the ImGui context exists; no style: the host's

void registerDashboardBlocks(gr::BlockRegistry& registry);
void registerVideoBlocks(gr::BlockRegistry& registry);        // SetupVideoBlocks.cpp: a translation unit of its own, for compile memory
void registerLocalisationBlocks(gr::BlockRegistry& registry); // SetupLocalisationBlocks.cpp, likewise

void applyStyle(LookAndFeel::Style style);
void applyTheme(LookAndFeel::Theme theme);

} // namespace DigitizerUi

#endif // OPENDIGITIZER_UI_SETUP_HPP
