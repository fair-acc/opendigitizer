#ifndef OPENDIGITIZER_CHARTS_DATASINKS_HPP
#define OPENDIGITIZER_CHARTS_DATASINKS_HPP

#include "SignalSink.hpp" // PlotPoint, PlotData, PlotGetter, SignalSink

#include <chrono>
#include <gnuradio-4.0/Tag.hpp>

namespace opendigitizer {

struct TagData {
    double           timestamp;
    gr::property_map map;
};

struct HistoryRequirement {
    std::size_t                           requiredSize;
    std::chrono::steady_clock::time_point lastUpdate;
    std::chrono::milliseconds             timeout;
};

} // namespace opendigitizer

// Backward compatibility aliases
namespace opendigitizer::sinks {
using TagData            = opendigitizer::TagData;
using HistoryRequirement = opendigitizer::HistoryRequirement;
} // namespace opendigitizer::sinks

namespace opendigitizer::charts {
using TagData            = opendigitizer::TagData;
using HistoryRequirement = opendigitizer::HistoryRequirement;
} // namespace opendigitizer::charts

#endif // OPENDIGITIZER_CHARTS_DATASINKS_HPP
