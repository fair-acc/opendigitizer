#ifndef OPENDIGITIZER_TEST_TESTSINKS_HPP
#define OPENDIGITIZER_TEST_TESTSINKS_HPP

#include "../charts/Charts.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace gr::od::test {

/// a chart's request never shrinks it below `capacity`
inline std::shared_ptr<SignalSink> makeTestStreamingSink(std::string name, std::size_t capacity = 2048) {
    auto sink      = std::make_shared<SignalSink>(SignalSink::Config{.kind = SignalKind::Streaming, .uniqueName = name, .name = name, .capacity = capacity, .capacityFloor = capacity});
    sink->metadata = {.signalQuantity = "voltage", .signalUnit = "V", .sampleRate = 1000.0f};
    return sink;
}

/// a chart's request never shrinks it below `maxDataSets`
inline std::shared_ptr<SignalSink> makeTestDataSetSink(std::string name, std::size_t maxDataSets = 10) { //
    return std::make_shared<SignalSink>(SignalSink::Config{.kind = SignalKind::Dataset1D, .uniqueName = name, .name = name, .capacity = maxDataSets, .capacityFloor = maxDataSets});
}

inline std::shared_ptr<charts::XYChart> makeXYChart(const std::string& name = "") {
    gr::property_map initParams;
    if (!name.empty()) {
        initParams["chart_name"] = name;
    }
    return std::make_shared<charts::XYChart>(std::move(initParams));
}

inline std::shared_ptr<charts::YYChart> makeYYChart(const std::string& name = "") {
    gr::property_map initParams;
    if (!name.empty()) {
        initParams["chart_name"] = name;
    }
    return std::make_shared<charts::YYChart>(std::move(initParams));
}

inline std::variant<std::shared_ptr<charts::XYChart>, std::shared_ptr<charts::YYChart>, std::monostate> makeChartByType(std::string_view typeName, const std::string& name = "") {
    using namespace charts;
    using Storage = std::variant<std::shared_ptr<XYChart>, std::shared_ptr<YYChart>, std::monostate>;

    std::string lowerTypeName(typeName);
    std::transform(lowerTypeName.begin(), lowerTypeName.end(), lowerTypeName.begin(), [](unsigned char c) { return std::tolower(c); });

    if (lowerTypeName.find("xychart") != std::string::npos || typeName == "XYChart") {
        return Storage{makeXYChart(name)};
    }
    if (lowerTypeName.find("yychart") != std::string::npos || typeName == "YYChart") {
        return Storage{makeYYChart(name)};
    }
    return Storage{std::monostate{}};
}

} // namespace gr::od::test

#endif // OPENDIGITIZER_TEST_TESTSINKS_HPP
