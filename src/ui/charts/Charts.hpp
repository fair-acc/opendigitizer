#ifndef OPENDIGITIZER_CHARTS_HPP
#define OPENDIGITIZER_CHARTS_HPP

/**
 * @file Charts.hpp
 * @brief Convenience header that includes all chart-related headers.
 *
 * Include this file to get access to:
 * - SignalSink interfaces and implementations
 * - Chart base class and managers
 * - XYChart and other chart implementations
 */

#include "Chart.hpp"
#include "DataSinks.hpp"
#include "SignalSink.hpp"
#include "XYChart.hpp"
#include "YYChart.hpp"

namespace opendigitizer::charts {

/**
 * @brief Helper function to create an XYChart (gr::Block-based).
 *
 * @param name Chart name (optional)
 * @return Shared pointer to the new XYChart
 */
inline std::shared_ptr<XYChart> makeXYChart(const std::string& name = "") {
    gr::property_map initParams;
    if (!name.empty()) {
        initParams["chart_name"] = name;
    }
    return std::make_shared<XYChart>(initParams);
}

/**
 * @brief Helper function to create a YYChart (correlation plot, gr::Block-based).
 *
 * @param name Chart name (optional)
 * @return Shared pointer to the new YYChart
 */
inline std::shared_ptr<YYChart> makeYYChart(const std::string& name = "") {
    gr::property_map initParams;
    if (!name.empty()) {
        initParams["chart_name"] = name;
    }
    return std::make_shared<YYChart>(initParams);
}

/**
 * @brief Create a chart by type name.
 *
 * Supported types: "XYChart", "YYChart" (or their fully-qualified names like "opendigitizer::charts::XYChart")
 *
 * @param typeName The chart type name
 * @param name Optional chart name
 * @return Pair of (shared_ptr for ownership, Chart* for polymorphic access)
 *         Both are null if typeName is unknown.
 */
inline std::pair<std::shared_ptr<void>, Chart*> makeChartByType(std::string_view typeName, const std::string& name = "") {
    // Handle both short names ("XYChart") and fully-qualified names ("opendigitizer::charts::XYChart")
    auto matchesType = [](std::string_view fullName, std::string_view shortName) {
        if (fullName == shortName) {
            return true;
        }
        // Check if fullName ends with "::ShortName"
        const std::string suffix = std::string("::") + std::string(shortName);
        return fullName.size() > suffix.size() && fullName.substr(fullName.size() - suffix.size()) == suffix;
    };

    if (matchesType(typeName, "XYChart")) {
        auto   chart    = makeXYChart(name);
        Chart* chartPtr = chart.get(); // XYChart inherits from Chart
        return {std::static_pointer_cast<void>(chart), chartPtr};
    }
    if (matchesType(typeName, "YYChart")) {
        auto   chart    = makeYYChart(name);
        Chart* chartPtr = chart.get(); // YYChart inherits from Chart
        return {std::static_pointer_cast<void>(chart), chartPtr};
    }
    return {std::shared_ptr<void>{}, nullptr};
}

/**
 * @brief Get the list of available chart type names.
 *
 * Returns the known chart types for UI dropdowns. In the future,
 * this could query gr::BlockRegistry for blocks with UICategory::ChartPane.
 */
inline std::vector<std::string> registeredChartTypes() { return {"XYChart", "YYChart"}; }

} // namespace opendigitizer::charts

#endif // OPENDIGITIZER_CHARTS_HPP
