#ifndef OPENDIGITIZER_UI_DASHBOARD_BLOCKS_HPP_
#define OPENDIGITIZER_UI_DASHBOARD_BLOCKS_HPP_

#include <optional>

#include <gnuradio-4.0/BlockRegistry.hpp>

#include "common/LookAndFeel.hpp"

namespace gr::od {

void initialise(std::optional<LookAndFeel::Style> style = {}); // once, after the ImGui context exists; no style: the host's

void registerDashboardBlocks(gr::BlockRegistry& registry);
void registerVideoBlocks(gr::BlockRegistry& registry);        // DashboardBlocksVideo.cpp: a translation unit of its own, for compile memory
void registerLocalisationBlocks(gr::BlockRegistry& registry); // DashboardBlocksLocalisation.cpp, likewise
void registerSignalBlocks(gr::BlockRegistry& registry);       // DashboardBlocksSignal.cpp, likewise
void registerSinkBlocks(gr::BlockRegistry& registry);         // DashboardBlocksSinks.cpp, likewise
void registerFrameSinkBlocks(gr::BlockRegistry& registry);    // DashboardBlocksSinkFrames.cpp: the DataSet and tensor forms, for compile memory

void applyStyle(LookAndFeel::Style style);
void applyTheme(LookAndFeel::Theme theme);

} // namespace gr::od

#endif // OPENDIGITIZER_UI_DASHBOARD_BLOCKS_HPP_
