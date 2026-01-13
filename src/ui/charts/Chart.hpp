#ifndef OPENDIGITIZER_CHARTS_CHART_HPP
#define OPENDIGITIZER_CHARTS_CHART_HPP

/**
 * @file Chart.hpp
 * @brief Backward-compatible header for chart types.
 *
 * This header is maintained for backward compatibility.
 * New code should include the specific headers directly:
 * - ChartInterface.hpp for the abstract chart interface
 * - ChartUtils.hpp for helper types and functions
 * - XYChart.hpp / YYChart.hpp for specific chart implementations
 *
 * The old Chart base class, ChartManager, ChartTypeRegistry, and ChartConfig
 * have been removed. Charts now use:
 * - ChartInterface: Abstract polymorphic interface for charts
 * - gr::Block<T>: Charts are proper GR4 blocks with GR_MAKE_REFLECTABLE
 * - gr::BlockRegistry: For dynamic chart type discovery (future)
 * - makeChartByType(): Factory function for creating charts
 */

#include "ChartInterface.hpp"
#include "ChartUtils.hpp"

#endif // OPENDIGITIZER_CHARTS_CHART_HPP
