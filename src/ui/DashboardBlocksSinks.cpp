#include "DashboardBlocks.hpp"

#include "blocks/ImPlotSink.hpp"
#include "blocks/TagSink.hpp"

#include <complex>
#include <cstdint>

namespace gr::od {

void registerSinkBlocks(gr::BlockRegistry& registry) {
    registerStreamSinks<std::int8_t, std::int16_t, std::int32_t, std::int64_t, std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t, float, double>(registry);
    registerStreamSinks<std::complex<float>, std::complex<double>>(registry);
    registerPlotSinks<gr::property_map>(registry);
    registerWithIdPrefix<TagSink, std::uint8_t, float>(registry, kOpendigitizerIdPrefix);
    registerFrameSinkBlocks(registry);
}

} // namespace gr::od
