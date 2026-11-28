#include "DashboardBlocks.hpp"

#include "blocks/ImPlotSink.hpp"

#include <complex>
#include <cstdint>

namespace gr::od {

void registerFrameSinkBlocks(gr::BlockRegistry& registry) {
    registerFrameSinks<std::int8_t, std::int16_t, std::int32_t, std::int64_t, std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t, float, double>(registry);
    registerPlotSinks<gr::DataSet<gr::UncertainValue<float>>, gr::DataSet<gr::UncertainValue<double>>>(registry);
    registerPlotSinks<gr::DataSet<std::complex<float>>, gr::DataSet<std::complex<double>>>(registry);
}

} // namespace gr::od
