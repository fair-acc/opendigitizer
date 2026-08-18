// Generates a nested tree of random .ddd dashboard files for manual testing
// Usage: generate_testdata <outputDir>
// Should usually be run via the `run_generate_testdata` cmake target

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <random>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gnuradio-4.0/YamlPmt.hpp>

namespace {

constexpr std::uint32_t               kSeed        = 0xD1617;
constexpr int                         kTargetFiles = 10'000;
constexpr int                         kMaxDepth    = 4;
constexpr std::chrono::year_month_day kBaseDate{std::chrono::year{2026}, std::chrono::month{8}, std::chrono::day{17}};

std::mt19937 rng(kSeed);

// from testDeviceNames in DeviceNameHelper.hpp
constexpr std::array<std::string_view, 50> kDeviceNames{                              //
    "GS11MU2", "GE01KS1", "YR03BG1E", "GE01BU1", "GE01KP02", "GE01KX3", "GE01QS1F",   //
    "GE02BE1", "GE02KY4", "GECD001", "GECEBG2T", "GECEKT1G", "GECEKY5C", "GEITQT11",  //
    "GHADKY2", "GHADMU1", "GHADQT51", "GHFSKS1", "GHHTMU3", "GHHTQD21", "GHTAKX1",    //
    "GHTBKH1", "GHTCQT22", "GHTYKV3", "GHTYMH1", "GS01KS1C", "GS02BB1F", "GS03KH1I",  //
    "GS04MU1A", "GS05KS3C", "GS07BE3", "GS08KM5SS", "GS10KX1", "GS12QS1F", "GSCD012", //
    "GSCEBG3D", "GSCEKY2G", "GTE2QT12", "GTE4MU1", "GTH3MK1", "GTP1KY1", "GTR2KX2",   //
    "GTR3QD41", "GTS3KY1", "GTS5QT12", "GTT1MU0", "GTV2QD11", "YR00QS1", "YR03KH3G",  //
    "YRT1KH1"};

constexpr std::array<std::string_view, 5> kChartTypes{
    "opendigitizer::charts::XYChart",       //
    "opendigitizer::charts::YYChart",       //
    "opendigitizer::charts::SpectrumPlot",  //
    "opendigitizer::charts::WaterfallPlot", //
    "opendigitizer::charts::SpectrumView",  //
};

constexpr std::array<std::uint32_t, 8> colorPalette{0x00C800, 0xC800C8, 0xC80000, 0x0000C8, 0xFF8C00, 0x1E90FF, 0x2E8B57, 0x8A2BE2};

constexpr std::array<std::string_view, 8> kHeaderTagPool{
    "commissioning", "nightshift", "experimental", "reference", //
    "obsolete", "shared", "critical", "review",                 //
};

constexpr std::array<std::pair<std::string_view, std::array<std::string_view, 3>>, 5> kHeaderKeyValueTagPool{{
    {"department", {"magnets", "rf cavity", "diagnostics"}},
    {"machine", {"sis18", "sis100", "unilac"}},
    {"device", {"GS11MU2", "GE01KS1", "YR03BG1E"}},
}};

constexpr std::array<std::string_view, 23> kFirstWords{
    "beam", "injection", "extraction", "spectrum", "magnet", "vacuum", "rf", //
    "cryo", "dipole", "quad", "kicker", "cavity", "orbit", "ramp", "cycle",  //
    "archive", "legacy", "backup", "storage", "ring", "linac", "target",     //
    "detector",                                                              //
};

constexpr std::array<std::string_view, 20> kSecondWords{
    "overview", "diagnostics", "monitor", "status", "summary", "watch",   //
    "trend", "analysis", "check", "scan", "profile", "history",           //
    "snapshot", "control", "current", "voltage", "intensity", "position", //
    "losses", "timing",                                                   //
};

constexpr std::array<std::string_view, 8> kDirectoryOnlySecondWords{"2023", "2024", "2025", "north", "south", "hall", "test", "main"};

int randomRangeInt(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }

double randomRangeReal(double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); }

bool percentChanceIsTrue(double probability) { return std::uniform_real_distribution<double>(0.0, 1.0)(rng) < probability; }

template<typename TRange>
const auto& chooseOneFrom(const TRange& range) {
    return range[static_cast<std::size_t>(randomRangeInt(0, static_cast<int>(range.size()) - 1))];
}

template<typename TRange>
std::vector<std::ranges::range_value_t<TRange>> randomlySelectAtMostItems(const TRange& pool, std::size_t count) {
    std::vector<std::ranges::range_value_t<TRange>> shuffled(pool.begin(), pool.end());
    std::ranges::shuffle(shuffled, rng);
    shuffled.resize(std::min(count, shuffled.size()));
    return shuffled;
}

std::string randomFolderName(std::set<std::string>& used) {
    while (true) {
        const bool        useDirectoryOnlySuffix = percentChanceIsTrue(0.25);
        const std::string name                   = std::format("{}_{}", chooseOneFrom(kFirstWords), //
            useDirectoryOnlySuffix ? chooseOneFrom(kDirectoryOnlySecondWords) : chooseOneFrom(kSecondWords));
        if (used.insert(name).second) {
            return name;
        }
    }
}

std::string randomFileName(std::set<std::string>& used) {
    while (true) {
        std::string name = std::format("{}_{}", chooseOneFrom(kFirstWords), chooseOneFrom(kSecondWords));
        if (percentChanceIsTrue(0.6)) {
            name += std::format("_{}", randomRangeInt(1, 99));
        }
        name += ".ddd";
        if (used.insert(name).second) {
            return name;
        }
    }
}

struct BlockSpec {
    std::string      id;
    std::string      name;
    gr::property_map parameters;
};

enum class SourceKind { sine, spectrum, remote, MAX };

std::vector<BlockSpec> makeSources() {
    std::vector<BlockSpec> sources;
    const int              nSources = randomRangeInt(1, 4);
    for (int i = 0; i < nSources; ++i) {
        const SourceKind kind = static_cast<SourceKind>(randomRangeInt(0, static_cast<int>(SourceKind::MAX) - 1));
        BlockSpec        source;
        switch (kind) {
        case SourceKind::sine:
            source.id                      = "opendigitizer::SineSource<float32>";
            source.name                    = std::format("sineSource{}", i + 1);
            source.parameters["frequency"] = static_cast<float>(randomRangeReal(0.05, 2.0));
            break;
        case SourceKind::spectrum:
            source.id   = "opendigitizer::TestSpectrumGenerator<float32>";
            source.name = std::format("spectrumSource{}", i + 1);
            break;
        case SourceKind::remote: {
            source.id                        = "opendigitizer::RemoteStreamSource<float32>";
            source.name                      = std::format("remoteSource{}", i + 1);
            const std::string_view signal    = chooseOneFrom(kDeviceNames);
            source.parameters["remote_uri"]  = std::format("https://demo.fair.gsi.de:8080/GnuRadio/Acquisition?channelNameFilter={}", signal);
            source.parameters["signal_name"] = std::string(signal);
            break;
        }
        case SourceKind::MAX: std::unreachable();
        }
        source.parameters["name"] = source.name;
        sources.push_back(std::move(source));
    }
    return sources;
}

std::vector<BlockSpec> makeSinks() {
    std::vector<BlockSpec> sinks;
    const int              nSinks = randomRangeInt(1, 5);
    for (int i = 0; i < nSinks; ++i) {
        BlockSpec sink{.id = "opendigitizer::ImPlotSink<float32>", .name = std::format("plotSink{}", i + 1), .parameters = {}};
        sink.parameters["name"] = sink.name;
        if (percentChanceIsTrue(0.15)) {
            sink.parameters["color"] = chooseOneFrom(colorPalette);
        }
        sinks.push_back(std::move(sink));
    }
    return sinks;
}

gr::pmt::Value buildDockTree(std::span<const std::string> names) {
    if (names.size() == 1) {
        return names.front();
    }
    const std::size_t cut = static_cast<std::size_t>(randomRangeInt(1, static_cast<int>(names.size()) - 1));
    gr::property_map  split;
    split["first"]  = buildDockTree(names.first(cut));
    split["second"] = buildDockTree(names.subspan(cut));
    split["ratio"]  = static_cast<float>(randomRangeReal(0.25, 0.75));
    return gr::property_map{{percentChanceIsTrue(0.5) ? "hsplit" : "vsplit", std::move(split)}};
}

gr::property_map makeWindowLayout(std::span<const std::string> plotNames) {
    std::vector<std::string> docked;
    std::vector<std::string> floating;
    for (const std::string& plotName : plotNames) {
        (percentChanceIsTrue(0.5) ? docked : floating).push_back(plotName);
    }

    gr::property_map floatingWindows;
    for (const std::string& plotName : floating) {
        const float width  = static_cast<float>(randomRangeInt(120, 380));
        const float height = static_cast<float>(randomRangeInt(120, 380));
        const float x      = static_cast<float>(randomRangeInt(0, std::min(300, 479 - static_cast<int>(width))));
        const float y      = static_cast<float>(randomRangeInt(0, std::min(300, 479 - static_cast<int>(height))));

        floatingWindows[std::pmr::string{plotName}] = gr::property_map{{"x", x}, {"y", y}, {"width", width}, {"height", height}};
    }

    gr::property_map windowLayout;
    windowLayout["floatingWindows"] = std::move(floatingWindows);
    if (docked.size() == 1) {
        windowLayout["dockSpace"] = gr::property_map{{"hsplit", gr::property_map{{"first", docked.front()}, {"ratio", 0.5f}}}};
    } else if (!docked.empty()) {
        windowLayout["dockSpace"] = buildDockTree(docked);
    }
    return windowLayout;
}

gr::property_map makeFlowgraph() {
    const std::vector<BlockSpec> sources = makeSources();
    const std::vector<BlockSpec> sinks   = makeSinks();

    gr::Tensor<gr::pmt::Value> blocks;
    for (const std::vector<BlockSpec>* blockList : {&sources, &sinks}) {
        for (const BlockSpec& block : *blockList) {
            blocks.emplace_back(gr::property_map{{"id", block.id}, {"parameters", block.parameters}});
        }
    }

    gr::Tensor<gr::pmt::Value> connections;
    for (const BlockSpec& sink : sinks) {
        if (!percentChanceIsTrue(0.85)) {
            continue;
        }
        gr::Tensor<gr::pmt::Value> connection;
        connection.emplace_back(chooseOneFrom(sources).name);
        connection.emplace_back(std::int64_t{0});
        connection.emplace_back(sink.name);
        connection.emplace_back(std::int64_t{0});
        connections.emplace_back(std::move(connection));
    }

    gr::Tensor<gr::pmt::Value> dashboardSources;
    for (const BlockSpec& sink : sinks) {
        dashboardSources.emplace_back(gr::property_map{{"name", sink.name}, {"block", sink.name}});
    }

    std::vector<std::string> plotNames;
    const int                nPlots = randomRangeInt(1, 4);
    for (int i = 0; i < nPlots; ++i) {
        plotNames.push_back(std::format("Plot {}", i + 1));
    }

    gr::Tensor<gr::pmt::Value> plots;
    for (const std::string& plotName : plotNames) {
        gr::Tensor<gr::pmt::Value> plotSinkNames;
        for (const BlockSpec& sink : randomlySelectAtMostItems(sinks, static_cast<std::size_t>(randomRangeInt(1, 3)))) {
            plotSinkNames.emplace_back(sink.name);
        }
        plots.emplace_back(gr::property_map{{"name", plotName}, {"type", std::string(chooseOneFrom(kChartTypes))}, {"sources", std::move(plotSinkNames)}});
    }

    gr::property_map dashboard;
    dashboard["layout"]       = std::string("Free");
    dashboard["sources"]      = std::move(dashboardSources);
    dashboard["plots"]        = std::move(plots);
    dashboard["windowLayout"] = makeWindowLayout(plotNames);

    gr::property_map flowgraph;
    flowgraph["blocks"]      = std::move(blocks);
    flowgraph["connections"] = std::move(connections);
    flowgraph["dashboard"]   = std::move(dashboard);
    return flowgraph;
}

gr::property_map makeHeader() {
    gr::property_map header;
    header["favorite"] = percentChanceIsTrue(0.25);
    if (!percentChanceIsTrue(0.15)) {
        const std::chrono::year_month_day lastUsed(std::chrono::sys_days(kBaseDate) - std::chrono::days(randomRangeInt(0, 730)));
        header["lastUsed"] = std::format("{:04}/{:02}/{:02}", static_cast<int>(lastUsed.year()), static_cast<unsigned>(lastUsed.month()), static_cast<unsigned>(lastUsed.day()));
    }

    gr::Tensor<gr::pmt::Value> tags;
    for (const std::string_view tag : randomlySelectAtMostItems(kHeaderTagPool, static_cast<std::size_t>(randomRangeInt(0, 4)))) {
        tags.emplace_back(std::string(tag));
    }
    header["tags"] = std::move(tags);

    gr::property_map keyValueTags;
    for (const auto& [key, values] : randomlySelectAtMostItems(kHeaderKeyValueTagPool, static_cast<std::size_t>(randomRangeInt(0, 3)))) {
        keyValueTags[std::string(key)] = std::string(chooseOneFrom(values));
    }
    header["keyValueTags"] = std::move(keyValueTags);
    return header;
}

void writeDddFile(const std::filesystem::path& path) {
    const gr::property_map flowgraph     = makeFlowgraph();
    const std::string      headerYaml    = gr::pmt::yaml::serialize(makeHeader());
    const std::string      flowgraphYaml = gr::pmt::yaml::serialize(flowgraph);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    const auto    writeU32 = [&stream](std::uint32_t value) { stream.write(reinterpret_cast<const char*>(&value), 4); };

    constexpr std::uint32_t headerStart = 32;
    const std::uint32_t     headerSize  = static_cast<std::uint32_t>(headerYaml.size());
    writeU32(headerStart);
    writeU32(headerSize);
    writeU32(0);
    writeU32(0);
    writeU32(headerStart + headerSize);
    writeU32(static_cast<std::uint32_t>(flowgraphYaml.size()));
    writeU32(0);
    writeU32(0);
    stream << headerYaml << flowgraphYaml;
}

int subtreeCapacity(int depth) {
    if (depth == kMaxDepth) {
        return 98;
    }
    return 8 + 90 * subtreeCapacity(depth + 1); // a few local files plus up to 90 subfolders
}

void buildDirectory(const std::filesystem::path& dirPath, int depth, int nFiles) {
    std::filesystem::create_directories(dirPath);
    std::set<std::string> usedNames;

    if (depth == kMaxDepth || nFiles <= randomRangeInt(20, 60)) {
        assert(3 < nFiles && nFiles < 99);
        for (int i = 0; i < nFiles; ++i) {
            writeDddFile(dirPath / randomFileName(usedNames));
        }
        return;
    }

    const int localFiles  = randomRangeInt(2, std::min(24, nFiles - 8));
    int       remaining   = nFiles - localFiles;
    const int childCap    = subtreeCapacity(depth + 1);
    const int minChildren = std::max(2, (remaining + childCap - 1) / childCap);
    const int maxChildren = std::min({90, remaining / 4, 96 - localFiles});
    const int nChildren   = std::min(maxChildren, std::max(minChildren, randomRangeInt(2, 9)));

    std::vector<int> parts(static_cast<std::size_t>(nChildren), 4);
    int              extra = remaining - 4 * nChildren;
    while (extra > 0) {
        int&      part = parts[static_cast<std::size_t>(randomRangeInt(0, nChildren - 1))];
        const int room = childCap - part;
        if (room == 0) {
            continue;
        }
        const int take = std::min({room, extra, randomRangeInt(1, std::max(1, extra / nChildren + 1))});
        part += take;
        extra -= take;
    }

    for (int i = 0; i < localFiles; ++i) {
        writeDddFile(dirPath / randomFileName(usedNames));
    }
    for (const int part : parts) {
        buildDirectory(dirPath / randomFolderName(usedNames), depth + 1, part);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::println(stderr, "usage: {} <outputDir>", argv[0]);
        return 1;
    }
    const std::filesystem::path root = argv[1];
    std::filesystem::create_directories(root);
    buildDirectory(root, 0, kTargetFiles);
    return 0;
}
