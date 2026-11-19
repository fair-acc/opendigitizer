#ifndef OPENDIGITIZER_CHARTS_BLOCKTYPES_HPP
#define OPENDIGITIZER_CHARTS_BLOCKTYPES_HPP

#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <gnuradio-4.0/meta/utils.hpp>

namespace gr::od {

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

/// block-id prefixes from before gr::od: OD blocks register under them, so saved .grc files and messages keep their ids
inline constexpr std::string_view kOpendigitizerIdPrefix = "opendigitizer::";
inline constexpr std::string_view kDigitizerUiIdPrefix   = "DigitizerUi::";

/// C++ type name -> registered block id; filled by registerWithIdPrefix()
inline std::map<std::string, std::string, std::less<>>& registeredIds() {
    static std::map<std::string, std::string, std::less<>> ids;
    return ids;
}

/// the id a block type is registered under; the type name itself if it has no other
[[nodiscard]] inline std::string_view registeredId(std::string_view typeName) {
    const auto found = registeredIds().find(typeName);
    return found == registeredIds().end() ? typeName : std::string_view(found->second);
}

template<typename TBlock>
[[nodiscard]] std::string_view registeredId() {
    return registeredId(gr::meta::type_name<TBlock>());
}

/// a default unique name "<type name>#<n>" spelled with the registered id, e.g. "opendigitizer::ImPlotSink<float32>#3"
[[nodiscard]] inline std::string registeredUniqueName(std::string_view uniqueName) {
    const auto hash = uniqueName.rfind('#');
    return hash == std::string_view::npos ? std::string(uniqueName) : std::format("{}{}", registeredId(uniqueName.substr(0UZ, hash)), uniqueName.substr(hash));
}

/// registers TBlock under `<idPrefix><its name below gr::od>`, besides the C++ type name GR4 always registers, and returns that id
template<typename TBlock>
const std::string& registerWithIdPrefix(auto& registry, std::string_view idPrefix) {
    constexpr std::string_view kTypeName  = gr::meta::type_name<TBlock>();
    constexpr std::string_view kNamespace = "gr::od::";
    static_assert(kTypeName.starts_with(kNamespace));
    std::string id = std::format("{}{}", idPrefix, kTypeName.substr(kNamespace.size()));
    std::ignore    = registry.template insert<TBlock>(std::format("={}", id));
    return registeredIds().try_emplace(std::string(kTypeName), std::move(id)).first->second;
}

template<template<typename...> typename TBlock, typename... TParameters>
void registerWithIdPrefix(auto& registry, std::string_view idPrefix) {
    (registerWithIdPrefix<TBlock<TParameters>>(registry, idPrefix), ...);
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

} // namespace gr::od

#endif
