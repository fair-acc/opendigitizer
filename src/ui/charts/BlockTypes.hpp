#ifndef OPENDIGITIZER_CHARTS_BLOCKTYPES_HPP
#define OPENDIGITIZER_CHARTS_BLOCKTYPES_HPP

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>

namespace opendigitizer {

enum class SignalKind : std::uint8_t {
    None      = 0,
    Streaming = 1 << 0U,
    Dataset1D = 1 << 1U,
    Image2D   = 1 << 2U,
    Events    = 1 << 3U,               // dated trigger events, for event charts only
    All       = Streaming | Dataset1D, // 1D signals; Image2D only for image charts
};
constexpr SignalKind operator|(SignalKind a, SignalKind b) noexcept { //
    return static_cast<SignalKind>(std::to_underlying(a) | std::to_underlying(b));
}
constexpr SignalKind operator&(SignalKind a, SignalKind b) noexcept { //
    return static_cast<SignalKind>(std::to_underlying(a) & std::to_underlying(b));
}

/// ImPlotSink block types by registered name -> whether the sink takes DataSets; filled by registerPlotSinks()
inline std::map<std::string, bool, std::less<>>& plotSinkTypes() {
    static std::map<std::string, bool, std::less<>> types;
    return types;
}

namespace charts {

/// chart block types by registered name -> the signals each displays; filled by registerChart()
inline std::map<std::string, SignalKind, std::less<>>& chartTypes() {
    static std::map<std::string, SignalKind, std::less<>> types;
    return types;
}

} // namespace charts

} // namespace opendigitizer

#endif
