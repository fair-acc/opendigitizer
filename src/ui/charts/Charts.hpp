#ifndef OPENDIGITIZER_UI_CHARTS_HPP
#define OPENDIGITIZER_UI_CHARTS_HPP

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
#include "SignalSink.hpp"
#include "SignalSinkImpl.hpp"
#include "XYChart.hpp"

namespace opendigitizer::ui::charts {

/**
 * @brief Helper function to create a streaming signal sink.
 */
template<typename T = float>
inline std::shared_ptr<StreamingSignalSink<T>> makeStreamingSink(std::string uniqueName, std::size_t initialCapacity = 2048) {
    return std::make_shared<StreamingSignalSink<T>>(std::move(uniqueName), initialCapacity);
}

/**
 * @brief Helper function to create a DataSet signal sink.
 */
template<typename T = float>
inline std::shared_ptr<DataSetSignalSink<T>> makeDataSetSink(std::string uniqueName, std::size_t maxDataSets = 10) {
    return std::make_shared<DataSetSignalSink<T>>(std::move(uniqueName), maxDataSets);
}

/**
 * @brief Helper function to create an XYChart.
 */
inline std::shared_ptr<XYChart> makeXYChart(const ChartConfig& config = {}) { return std::make_shared<XYChart>(config); }

/**
 * @brief Helper function to create a chart by type name.
 */
inline std::unique_ptr<Chart> makeChart(std::string_view typeName, const ChartConfig& config = {}) { return ChartTypeRegistry::instance().create(typeName, config); }

} // namespace opendigitizer::ui::charts

#endif // OPENDIGITIZER_UI_CHARTS_HPP
