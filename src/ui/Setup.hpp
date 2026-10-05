#ifndef OPENDIGITIZER_UI_SETUP_HPP
#define OPENDIGITIZER_UI_SETUP_HPP

#include <optional>

#include <gnuradio-4.0/BlockRegistry.hpp>

#include "common/LookAndFeel.hpp"

namespace DigitizerUi {

void initialise(std::optional<LookAndFeel::Style> style = {}); // once, after the ImGui context exists; no style: the host's

void registerDashboardBlocks(gr::BlockRegistry& registry);

void applyStyle(LookAndFeel::Style style);

} // namespace DigitizerUi

#endif // OPENDIGITIZER_UI_SETUP_HPP
