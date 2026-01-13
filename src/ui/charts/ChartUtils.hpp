#ifndef OPENDIGITIZER_CHARTS_CHARTUTILS_HPP
#define OPENDIGITIZER_CHARTS_CHARTUTILS_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <implot.h>

#include <gnuradio-4.0/Tag.hpp>

#include "SignalSink.hpp"

namespace opendigitizer::charts {

/**
 * @brief X-axis display mode for time-series data.
 */
enum class XAxisMode : std::uint8_t { UtcTime, RelativeTime, SampleIndex };

/**
 * @brief Axis scale modes for chart rendering.
 */
enum class AxisScale {
    Linear = 0,    // Standard linear scale [min, max]
    LinearReverse, // Reversed linear scale [max, min]
    Time,          // Datetime/timestamp scale
    Log10,         // Logarithmic base 10
    SymLog         // Symmetric log (handles negative values)
};

/**
 * @brief Label formatting options for axis values.
 */
enum class LabelFormat {
    Auto = 0,     // Automatic based on range
    Metric,       // SI prefixes (k, M, G, etc.)
    MetricInline, // SI prefixes inline with value
    Scientific,   // Scientific notation
    None,         // No labels
    Default       // Default floating-point format
};

/**
 * @brief Configuration for a single chart axis.
 */
struct AxisConfig {
    float         min    = std::numeric_limits<float>::lowest();
    float         max    = std::numeric_limits<float>::max();
    AxisScale     scale  = AxisScale::Linear;
    LabelFormat   format = LabelFormat::Auto;
    std::string   label;
    std::string   unit;
    bool          autoScale = true;
    bool          showGrid  = true;
    std::uint32_t color     = 0xFFFFFFFF;
};

/**
 * @brief External axis configuration from Dashboard.
 */
struct DashboardAxisConfig {
    enum class AxisKind { X = 0, Y };

    AxisKind    axis     = AxisKind::X;
    float       min      = std::numeric_limits<float>::quiet_NaN();
    float       max      = std::numeric_limits<float>::quiet_NaN();
    AxisScale   scale    = AxisScale::Linear;
    LabelFormat format   = LabelFormat::Auto;
    float       width    = std::numeric_limits<float>::max();
    bool        plotTags = true;
};

/**
 * @brief Represents a category for axis grouping.
 */
struct AxisCategory {
    std::string   quantity;              // Physical quantity (e.g., "voltage", "frequency")
    std::string   unit;                  // Unit of measurement (e.g., "V", "Hz")
    std::uint32_t color    = 0xFFFFFFFF; // Color for axis labels/ticks
    AxisScale     scale    = AxisScale::Linear;
    bool          plotTags = true;

    [[nodiscard]] bool matches(std::string_view q, std::string_view u) const noexcept { return quantity == q && unit == u; }

    [[nodiscard]] std::string buildLabel() const {
        if (!quantity.empty() && !unit.empty()) {
            return quantity + " [" + unit + "]";
        }
        if (!quantity.empty()) {
            return quantity;
        }
        if (!unit.empty()) {
            return unit;
        }
        return "";
    }
};

/**
 * @brief Axis formatting and setup utilities.
 *
 * Free functions for chart axis configuration, shared by all chart types.
 */
namespace axis {

/**
 * @brief Helper to null-terminate a format result.
 */
template<typename Result>
inline int enforceNullTerminate(char* buff, int size, const Result& result) {
    if ((result.out - buff) < static_cast<std::ptrdiff_t>(size)) {
        *result.out = '\0';
    } else if (size > 0) {
        buff[size - 1] = '\0';
    }
    return static_cast<int>(result.out - buff);
}

/**
 * @brief Safe string_view from char pointer with max length.
 */
inline constexpr std::string_view boundedStringView(const char* ptr, std::size_t maxLength) {
    if (!ptr) {
        return "";
    }
    for (std::size_t i = 0UZ; i < maxLength; ++i) {
        if (ptr[i] == '\0') {
            return std::string_view(ptr, i);
        }
    }
    return "";
}

/**
 * @brief Format a value using metric prefixes (k, M, G, etc.).
 * Compatible with ImPlot axis formatter callback.
 */
inline int formatMetric(double value, char* buff, int size, void* data) {
    constexpr std::array<double, 11UZ>      kScales{1e15, 1e12, 1e9, 1e6, 1e3, 1.0, 1e-3, 1e-6, 1e-9, 1e-12, 1e-15};
    constexpr std::array<const char*, 11UZ> kPrefixes{"P", "T", "G", "M", "k", "", "m", "u", "n", "p", "f"};
    constexpr std::size_t                   maxUnitLength = 10UZ;

    const std::string_view unit = boundedStringView(static_cast<const char*>(data), maxUnitLength);
    if (value == 0.0) {
        return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::size_t>(size), "0{}", unit));
    }

    std::string_view prefix      = kPrefixes.back();
    double           scaledValue = value / kScales.back();

    for (auto [scale, p] : std::views::zip(kScales, kPrefixes)) {
        if (std::abs(value) >= scale) {
            scaledValue = value / scale;
            prefix      = p;
            break;
        }
    }

    return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::size_t>(size), "{:g}{}{}", scaledValue, prefix, unit));
}

/**
 * @brief Format a value using minimal scientific notation.
 */
inline std::string formatMinimalScientific(double value, int maxDecimals = 2) {
    if (value == 0.0) {
        return "0";
    }

    const int    exponent = static_cast<int>(std::floor(std::log10(std::abs(value))));
    const double mantissa = value / std::pow(10.0, exponent);

    std::string mantissaStr = std::format("{:.{}f}", mantissa, maxDecimals);
    while (!mantissaStr.empty() && mantissaStr.back() == '0') {
        mantissaStr.pop_back();
    }
    if (!mantissaStr.empty() && mantissaStr.back() == '.') {
        mantissaStr.pop_back();
    }

    return std::format("{}E{}", mantissaStr, exponent);
}

/**
 * @brief Format a value using scientific notation.
 * Compatible with ImPlot axis formatter callback.
 */
inline int formatScientific(double value, char* buff, int size, void* data) {
    constexpr std::size_t  maxUnitLength = 10UZ;
    const std::string_view unit          = boundedStringView(static_cast<const char*>(data), maxUnitLength);

    if (value == 0.0) {
        return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::size_t>(size), "0{}", unit));
    }

    const double absVal = std::abs(value);
    if (absVal >= 1e-3 && absVal < 10000.0) {
        return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::size_t>(size), "{:.3g}{}", value, unit));
    } else {
        return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::size_t>(size), "{}{}", formatMinimalScientific(value), unit));
    }
}

/**
 * @brief Format a value using default floating-point format.
 * Compatible with ImPlot axis formatter callback.
 */
inline int formatDefault(double value, char* buff, int size, void* data) {
    constexpr std::size_t  maxUnitLength = 10UZ;
    const std::string_view unit          = boundedStringView(static_cast<const char*>(data), maxUnitLength);

    if (value == 0.0) {
        return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::size_t>(size), "0{}", unit));
    }

    return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::size_t>(size), "{:g}{}", value, unit));
}

/**
 * @brief Truncate a label to fit within available width.
 */
inline std::string truncateLabel(std::string_view original, float availableWidth) {
    const float textWidth = ImGui::CalcTextSize(original.data()).x;
    if (textWidth <= availableWidth) {
        return std::string(original);
    }
    static const float ellipsisWidth = ImGui::CalcTextSize("...").x;
    if (availableWidth <= ellipsisWidth + 1.f) {
        return "...";
    }
    const float       scaleFactor  = (availableWidth - ellipsisWidth) / std::max(1.f, textWidth);
    const std::size_t fitCharCount = static_cast<std::size_t>(std::floor(scaleFactor * static_cast<float>(original.size())));
    return std::format("...{}", original.substr(original.size() - fitCharCount));
}

/**
 * @brief Setup a single ImPlot axis from category info.
 *
 * @param axisId The ImPlot axis ID (ImAxis_X1, ImAxis_Y1, etc.)
 * @param category The axis category (quantity, unit, color)
 * @param format The label format to use
 * @param axisWidth Available width for axis label (for truncation)
 * @param minLimit Optional minimum axis limit (NaN for auto)
 * @param maxLimit Optional maximum axis limit (NaN for auto)
 * @param nTotalAxes Number of active axes in this direction (for color coding)
 * @param scaleOverride Axis scale (Linear, Log10, Time, etc.)
 * @param unitStringStorage Reference to storage for unit string (must outlive the plot)
 */
inline void setupAxis(ImAxis axisId, const std::optional<AxisCategory>& category, LabelFormat format, float axisWidth, double minLimit, double maxLimit, std::size_t nTotalAxes, AxisScale scaleOverride, std::string& unitStringStorage) {
    if (!category.has_value()) {
        return;
    }

    const bool isX       = axisId == ImAxis_X1 || axisId == ImAxis_X2 || axisId == ImAxis_X3;
    const bool finiteMin = std::isfinite(minLimit);
    const bool finiteMax = std::isfinite(maxLimit);
    const auto scale     = scaleOverride;

    // Determine axis flags based on limits
    ImPlotAxisFlags flags = ImPlotAxisFlags_None;
    if (finiteMin && !finiteMax) {
        flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_LockMin;
    } else if (!finiteMin && finiteMax) {
        flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_LockMax;
    } else if (!finiteMin && !finiteMax) {
        flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
    }

    // Opposite side for secondary axes
    if ((axisId == ImAxis_X2) || (axisId == ImAxis_X3) || (axisId == ImAxis_Y2) || (axisId == ImAxis_Y3)) {
        flags |= ImPlotAxisFlags_Opposite;
    }

    // Color coding for multiple Y-axes
    bool pushedColor = false;
    if ((nTotalAxes > 1) && !isX) {
        const auto   colorU32toImVec4 = [](std::uint32_t c) { return ImVec4{float((c >> 16) & 0xFF) / 255.f, float((c >> 8) & 0xFF) / 255.f, float((c >> 0) & 0xFF) / 255.f, 1.f}; };
        const ImVec4 col              = colorU32toImVec4(category->color);
        ImPlot::PushStyleColor(ImPlotCol_AxisText, col);
        ImPlot::PushStyleColor(ImPlotCol_AxisTick, col);
        pushedColor = true;
    }

    // Build label (empty for Time scale or MetricInline format)
    const std::string label = (scale == AxisScale::Time) || (format == LabelFormat::MetricInline) ? "" : truncateLabel(category->buildLabel(), axisWidth);

    ImPlot::SetupAxis(axisId, label.c_str(), flags);

    // Setup axis format
    if (scale != AxisScale::Time) {
        constexpr std::array kMetricUnits{"s", "m", "A", "K", "V", "g", "eV", "Hz"};
        constexpr std::array kLinearUnits{"dB"};
        const std::string&   unit = category->unit;

        switch (format) {
        case LabelFormat::Auto:
            if (std::ranges::contains(kMetricUnits, unit)) {
                ImPlot::SetupAxisFormat(axisId, formatMetric, nullptr);
            } else if (std::ranges::contains(kLinearUnits, unit)) {
                ImPlot::SetupAxisFormat(axisId, formatDefault, nullptr);
            } else if (isX) {
                constexpr const char* str = "s";
                ImPlot::SetupAxisFormat(axisId, formatMetric, const_cast<void*>(static_cast<const void*>(str)));
            } else {
                ImPlot::SetupAxisFormat(axisId, formatScientific, nullptr);
            }
            break;
        case LabelFormat::Metric: ImPlot::SetupAxisFormat(axisId, formatMetric, nullptr); break;
        case LabelFormat::MetricInline: {
            unitStringStorage = unit;
            ImPlot::SetupAxisFormat(axisId, formatMetric, const_cast<void*>(static_cast<const void*>(unitStringStorage.c_str())));
        } break;
        case LabelFormat::Scientific: ImPlot::SetupAxisFormat(axisId, formatScientific, nullptr); break;
        case LabelFormat::None:
        case LabelFormat::Default:
        default: ImPlot::SetupAxisFormat(axisId, formatDefault, nullptr);
        }
    }

    // Setup axis scale
    switch (scale) {
    case AxisScale::Log10: ImPlot::SetupAxisScale(axisId, ImPlotScale_Log10); break;
    case AxisScale::SymLog: ImPlot::SetupAxisScale(axisId, ImPlotScale_SymLog); break;
    case AxisScale::Time:
        ImPlot::GetStyle().UseISO8601     = true;
        ImPlot::GetStyle().Use24HourClock = true;
        ImPlot::SetupAxisScale(axisId, ImPlotScale_Time);
        break;
    default: ImPlot::SetupAxisScale(axisId, ImPlotScale_Linear); break;
    }

    // Setup axis limits
    if (finiteMin && finiteMax) {
        ImPlot::SetupAxisLimits(axisId, minLimit, maxLimit);
    } else if (finiteMin || finiteMax) {
        const double minConstraint = finiteMin ? minLimit : -std::numeric_limits<double>::infinity();
        const double maxConstraint = finiteMax ? maxLimit : +std::numeric_limits<double>::infinity();
        ImPlot::SetupAxisLimitsConstraints(axisId, minConstraint, maxConstraint);
    }

    if (pushedColor) {
        ImPlot::PopStyleColor(2);
    }
}

/**
 * @brief Find or create a category slot for the given quantity/unit.
 * @return The index of the category, or nullopt if no slots available.
 */
inline std::optional<std::size_t> findOrCreateCategory(std::array<std::optional<AxisCategory>, 3>& categories, std::string_view quantity, std::string_view unit, std::uint32_t color) {
    // Look for existing category with matching quantity+unit
    for (std::size_t i = 0; i < categories.size(); ++i) {
        if (categories[i].has_value() && categories[i]->matches(quantity, unit)) {
            return i;
        }
    }

    // Create new category in first empty slot
    for (std::size_t i = 0; i < categories.size(); ++i) {
        if (!categories[i].has_value()) {
            categories[i] = AxisCategory{
                .quantity = std::string(quantity),
                .unit     = std::string(unit),
                .color    = color,
            };
            return i;
        }
    }

    return std::nullopt; // No slots available
}

/**
 * @brief Build axis categories from signal sinks.
 *
 * Groups signals by their quantity+unit onto axes. Signals with
 * the same quantity+unit share the same axis. Up to 3 axes per direction.
 */
inline void buildAxisCategories(const std::vector<std::shared_ptr<SignalSink>>& signalSinks, std::array<std::optional<AxisCategory>, 3>& xCategories, std::array<std::optional<AxisCategory>, 3>& yCategories, std::array<std::vector<std::string>, 3>& xAxisGroups, std::array<std::vector<std::string>, 3>& yAxisGroups) {
    // Reset categories and groups
    for (auto& cat : xCategories) {
        cat.reset();
    }
    for (auto& cat : yCategories) {
        cat.reset();
    }
    for (auto& group : xAxisGroups) {
        group.clear();
    }
    for (auto& group : yAxisGroups) {
        group.clear();
    }

    // Group signals by quantity/unit
    for (const auto& sink : signalSinks) {
        const std::string sinkName = std::string(sink->uniqueName());

        // X-axis: group by abscissa quantity/unit
        if (auto idx = findOrCreateCategory(xCategories, sink->abscissaQuantity(), sink->abscissaUnit(), sink->color())) {
            xAxisGroups[*idx].push_back(sinkName);
        }

        // Y-axis: group by signal quantity/unit
        if (auto idx = findOrCreateCategory(yCategories, sink->signalQuantity(), sink->signalUnit(), sink->color())) {
            yAxisGroups[*idx].push_back(sinkName);
        }
    }
}

/**
 * @brief Find the axis index for a given signal sink.
 * @param sinkName The unique name of the sink.
 * @param isXAxis true for X-axis, false for Y-axis.
 * @param xAxisGroups X-axis group assignments
 * @param yAxisGroups Y-axis group assignments
 * @return The axis index (0-2), or 0 if not found.
 */
inline std::size_t findAxisForSink(std::string_view sinkName, bool isXAxis, const std::array<std::vector<std::string>, 3>& xAxisGroups, const std::array<std::vector<std::string>, 3>& yAxisGroups) {
    const auto& groups = isXAxis ? xAxisGroups : yAxisGroups;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        if (std::ranges::find(groups[i], sinkName) != groups[i].end()) {
            return i;
        }
    }
    return 0; // Default to first axis
}

/**
 * @brief Get the number of active axis categories.
 */
inline std::size_t activeAxisCount(const std::array<std::optional<AxisCategory>, 3>& categories) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(categories, [](const auto& c) { return c.has_value(); }));
}

} // namespace axis

// --- Tag Rendering Utilities ---

namespace tags {

/**
 * @brief Transform X coordinate based on axis scale.
 */
inline double transformX(double xVal, AxisScale axisScale, double xMin, double xMax, bool isDataSet = false) {
    if (isDataSet) {
        return xVal; // DataSets use absolute x values
    }
    switch (axisScale) {
    case AxisScale::Time: return xVal;
    case AxisScale::LinearReverse: return xVal - xMax;
    default: return xVal - xMin;
    }
}

/**
 * @brief Render vertical label at a tag position.
 */
inline ImVec2 plotVerticalTagLabel(std::string_view label, double xData, const ImPlotRect& plotLimits, bool plotLeft, double fractionBelowTop = 0.02, double sizeRatioLimit = 0.75) {
    const double yRange   = std::abs(plotLimits.Y.Max - plotLimits.Y.Min);
    const double ySafeTop = plotLimits.Y.Max - fractionBelowTop * yRange;
    const double yClamped = std::clamp(ySafeTop, plotLimits.Y.Min, plotLimits.Y.Max);
    ImVec2       pixelPos = ImPlot::PlotToPixels(xData, yClamped);
    if (label.empty()) {
        return pixelPos;
    }

    const double yPixelRange = static_cast<double>(std::abs(ImPlot::PlotToPixels(0.0, plotLimits.Y.Max).y - ImPlot::PlotToPixels(0.0, plotLimits.Y.Min).y));
    ImVec2       textSize    = ImGui::CalcTextSize(label.data());
    if (static_cast<double>(textSize.x) > sizeRatioLimit * yPixelRange) {
        return pixelPos; // text too large
    }

    ImVec2 pixOffset{plotLeft ? (-textSize.y + 2.0f) : (+5.0f), textSize.x};
    ImPlot::PlotText(label.data(), xData, yClamped, pixOffset, static_cast<int>(ImPlotTextFlags_Vertical) | static_cast<int>(ImPlotItemFlags_NoFit));
    return {pixelPos.x + pixOffset.x + textSize.y, pixelPos.y};
}

/**
 * @brief Draw streaming tags for a signal sink.
 *
 * @param sink The signal sink containing tags
 * @param axisScale The X-axis scale type
 * @param xMin Minimum x value of the data range
 * @param xMax Maximum x value of the data range
 * @param baseColor Base color for the signal (tags will be semi-transparent)
 */
inline void drawStreamingTags(const SignalSink& sink, AxisScale axisScale, double xMin, double xMax, ImVec4 baseColor) {
    if (!sink.hasStreamingTags()) {
        return;
    }

    ImVec4 tagColor = baseColor;
    tagColor.w *= 0.35f; // semi-transparent

    const float fontHeight  = ImGui::GetFontSize();
    const auto  plotLimits  = ImPlot::GetPlotLimits(IMPLOT_AUTO, IMPLOT_AUTO);
    const float yPixelRange = std::abs(ImPlot::PlotToPixels(0.0, plotLimits.Y.Max).y - ImPlot::PlotToPixels(0.0, plotLimits.Y.Min).y);

    ImGui::PushStyleColor(ImGuiCol_Text, tagColor);

    float lastTextPixelX = ImPlot::PlotToPixels(transformX(std::min(xMin, xMax), axisScale, xMin, xMax), 0.0).x;
    float lastAxisPixelX = ImPlot::PlotToPixels(transformX(std::max(xMin, xMax), axisScale, xMin, xMax), 0.0).x;

    sink.forEachTag([&](double timestamp, const gr::property_map& properties) {
        // Skip tags outside visible range
        if (timestamp < std::min(xMin, xMax)) {
            return;
        }

        double      xTagPosition = transformX(timestamp, axisScale, xMin, xMax);
        const float xPixelPos    = ImPlot::PlotToPixels(xTagPosition, 0.0).x;

        ImPlot::SetNextLineStyle(tagColor);
        ImPlot::PlotInfLines("", &xTagPosition, 1, ImPlotInfLinesFlags_None);

        // Draw label if space allows
        if ((xPixelPos - lastTextPixelX) > 1.5f * fontHeight && (lastAxisPixelX - xPixelPos) > 2.0f * fontHeight) {
            std::string triggerLabel = "TRIGGER";
            if (auto it = properties.find(std::string(gr::tag::TRIGGER_NAME.shortKey())); it != properties.end()) {
                if (auto* str = std::get_if<std::string>(&it->second)) {
                    triggerLabel = *str;
                }
            }

            const ImVec2 triggerLabelSize = ImGui::CalcTextSize(triggerLabel.c_str());
            if (triggerLabelSize.x < 0.75f * yPixelRange) {
                lastTextPixelX = plotVerticalTagLabel(triggerLabel, xTagPosition, plotLimits, true).x;

                // Also draw context if different
                if (auto it = properties.find(std::string(gr::tag::CONTEXT.shortKey())); it != properties.end()) {
                    if (auto* str = std::get_if<std::string>(&it->second); str && !str->empty() && *str != triggerLabel) {
                        const ImVec2 ctxLabelSize = ImGui::CalcTextSize(str->c_str());
                        if (ctxLabelSize.x < 0.75f * yPixelRange) {
                            lastTextPixelX = plotVerticalTagLabel(*str, xTagPosition, plotLimits, false).x;
                        }
                    }
                }
            }
        }
    });

    ImGui::PopStyleColor();
}

/**
 * @brief Draw timing events from a DataSet.
 *
 * @param dataSet The DataSet containing timing_events
 * @param axisScale The X-axis scale type
 * @param baseColor Base color for the signal
 */
template<typename T>
inline void drawDataSetTimingEvents(const gr::DataSet<T>& dataSet, [[maybe_unused]] AxisScale axisScale, ImVec4 baseColor) {
    if (dataSet.timing_events.empty() || dataSet.axisValues(0).empty()) {
        return;
    }

    ImVec4 tagColor = baseColor;
    tagColor.w *= 0.35f;

    const auto&  xAxisSpan   = dataSet.axisValues(0);
    const double xMin        = static_cast<double>(xAxisSpan.front());
    const double xMax        = static_cast<double>(xAxisSpan.back());
    const float  fontHeight  = ImGui::GetFontSize();
    const auto   plotLimits  = ImPlot::GetPlotLimits(IMPLOT_AUTO, IMPLOT_AUTO);
    const float  yPixelRange = std::abs(ImPlot::PlotToPixels(0.0, plotLimits.Y.Max).y - ImPlot::PlotToPixels(0.0, plotLimits.Y.Min).y);

    ImGui::PushStyleColor(ImGuiCol_Text, tagColor);

    float lastTextPixelX = ImPlot::PlotToPixels(xMin, 0.0).x;
    float lastAxisPixelX = ImPlot::PlotToPixels(xMax, 0.0).x;

    for (const auto& eventsForSig : dataSet.timing_events) {
        for (const auto& [xIndex, tagMap] : eventsForSig) {
            if (xIndex < 0 || static_cast<std::size_t>(xIndex) >= xAxisSpan.size()) {
                continue;
            }

            double      xVal         = static_cast<double>(xAxisSpan[static_cast<std::size_t>(xIndex)]);
            double      xTagPosition = xVal; // DataSets use absolute x values
            const float xPixelPos    = ImPlot::PlotToPixels(xTagPosition, 0.0).x;

            ImPlot::SetNextLineStyle(tagColor);
            ImPlot::PlotInfLines("", &xTagPosition, 1, ImPlotInfLinesFlags_None);

            if ((xPixelPos - lastTextPixelX) > 1.5f * fontHeight && (lastAxisPixelX - xPixelPos) > 2.0f * fontHeight) {
                std::string triggerLabel = "TRIGGER";
                if (auto it = tagMap.find(std::string(gr::tag::TRIGGER_NAME.shortKey())); it != tagMap.end()) {
                    if (auto* str = std::get_if<std::string>(&it->second)) {
                        triggerLabel = *str;
                    }
                }

                const ImVec2 triggerLabelSize = ImGui::CalcTextSize(triggerLabel.c_str());
                if (triggerLabelSize.x < 0.75f * yPixelRange) {
                    lastTextPixelX = plotVerticalTagLabel(triggerLabel, xTagPosition, plotLimits, true).x;
                }
            }
        }
    }

    ImGui::PopStyleColor();
}

} // namespace tags

} // namespace opendigitizer::charts

#endif // OPENDIGITIZER_CHARTS_CHARTUTILS_HPP
