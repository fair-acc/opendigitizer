#ifndef OPENDIGITIZER_CHARTS_CHART_HPP
#define OPENDIGITIZER_CHARTS_CHART_HPP

#include "SignalSink.hpp"
#include "SinkRegistry.hpp"

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/Tag.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <implot.h>
#include <implot_internal.h>

namespace opendigitizer::charts {

// ============================================================================
// Enums
// ============================================================================

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

// ============================================================================
// Axis Configuration Structs
// ============================================================================

/**
 * @brief Configuration for a single chart axis.
 */
struct AxisConfig {
    float         min       = std::numeric_limits<float>::lowest();
    float         max       = std::numeric_limits<float>::max();
    AxisScale     scale     = AxisScale::Linear;
    LabelFormat   format    = LabelFormat::Auto;
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

// ============================================================================
// Axis Formatting and Setup Utilities
// ============================================================================

namespace axis {

template<typename Result>
inline int enforceNullTerminate(char* buff, int size, const Result& result) {
    if ((result.out - buff) < static_cast<std::ptrdiff_t>(size)) {
        *result.out = '\0';
    } else if (size > 0) {
        buff[size - 1] = '\0';
    }
    return static_cast<int>(result.out - buff);
}

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

inline int formatDefault(double value, char* buff, int size, void* data) {
    constexpr std::size_t  maxUnitLength = 10UZ;
    const std::string_view unit          = boundedStringView(static_cast<const char*>(data), maxUnitLength);

    if (value == 0.0) {
        return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::size_t>(size), "0{}", unit));
    }
    return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::size_t>(size), "{:g}{}", value, unit));
}

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

inline void setupAxis(ImAxis axisId, const std::optional<AxisCategory>& category, LabelFormat format, float axisWidth, double minLimit, double maxLimit, std::size_t nTotalAxes, AxisScale scaleOverride, std::string& unitStringStorage) {
    if (!category.has_value()) {
        return;
    }

    const bool isX       = axisId == ImAxis_X1 || axisId == ImAxis_X2 || axisId == ImAxis_X3;
    const bool finiteMin = std::isfinite(minLimit);
    const bool finiteMax = std::isfinite(maxLimit);
    const auto scale     = scaleOverride;

    ImPlotAxisFlags flags = ImPlotAxisFlags_None;
    if (finiteMin && !finiteMax) {
        flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_LockMin;
    } else if (!finiteMin && finiteMax) {
        flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_LockMax;
    } else if (!finiteMin && !finiteMax) {
        flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
    }

    if ((axisId == ImAxis_X2) || (axisId == ImAxis_X3) || (axisId == ImAxis_Y2) || (axisId == ImAxis_Y3)) {
        flags |= ImPlotAxisFlags_Opposite;
    }

    bool pushedColor = false;
    if ((nTotalAxes > 1) && !isX) {
        const auto   colorU32toImVec4 = [](std::uint32_t c) { return ImVec4{float((c >> 16) & 0xFF) / 255.f, float((c >> 8) & 0xFF) / 255.f, float((c >> 0) & 0xFF) / 255.f, 1.f}; };
        const ImVec4 col              = colorU32toImVec4(category->color);
        ImPlot::PushStyleColor(ImPlotCol_AxisText, col);
        ImPlot::PushStyleColor(ImPlotCol_AxisTick, col);
        pushedColor = true;
    }

    const std::string label = (scale == AxisScale::Time) || (format == LabelFormat::MetricInline) ? "" : truncateLabel(category->buildLabel(), axisWidth);
    ImPlot::SetupAxis(axisId, label.c_str(), flags);

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

inline std::optional<std::size_t> findOrCreateCategory(std::array<std::optional<AxisCategory>, 3>& categories, std::string_view quantity, std::string_view unit, std::uint32_t color) {
    for (std::size_t i = 0; i < categories.size(); ++i) {
        if (categories[i].has_value() && categories[i]->matches(quantity, unit)) {
            return i;
        }
    }
    for (std::size_t i = 0; i < categories.size(); ++i) {
        if (!categories[i].has_value()) {
            categories[i] = AxisCategory{.quantity = std::string(quantity), .unit = std::string(unit), .color = color};
            return i;
        }
    }
    return std::nullopt;
}

inline void buildAxisCategories(const std::vector<std::shared_ptr<SignalSink>>& signalSinks, std::array<std::optional<AxisCategory>, 3>& xCategories, std::array<std::optional<AxisCategory>, 3>& yCategories, std::array<std::vector<std::string>, 3>& xAxisGroups, std::array<std::vector<std::string>, 3>& yAxisGroups) {
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

    for (const auto& sink : signalSinks) {
        const std::string sinkName = std::string(sink->uniqueName());
        if (auto idx = findOrCreateCategory(xCategories, sink->abscissaQuantity(), sink->abscissaUnit(), sink->color())) {
            xAxisGroups[*idx].push_back(sinkName);
        }
        if (auto idx = findOrCreateCategory(yCategories, sink->signalQuantity(), sink->signalUnit(), sink->color())) {
            yAxisGroups[*idx].push_back(sinkName);
        }
    }
}

inline std::size_t findAxisForSink(std::string_view sinkName, bool isXAxis, const std::array<std::vector<std::string>, 3>& xAxisGroups, const std::array<std::vector<std::string>, 3>& yAxisGroups) {
    const auto& groups = isXAxis ? xAxisGroups : yAxisGroups;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        if (std::ranges::find(groups[i], sinkName) != groups[i].end()) {
            return i;
        }
    }
    return 0;
}

inline std::size_t activeAxisCount(const std::array<std::optional<AxisCategory>, 3>& categories) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(categories, [](const auto& c) { return c.has_value(); }));
}

} // namespace axis

// ============================================================================
// Tag Rendering Utilities
// ============================================================================

namespace tags {

inline double transformX(double xVal, AxisScale axisScale, double xMin, double xMax, bool isDataSet = false) {
    if (isDataSet) {
        return xVal;
    }
    switch (axisScale) {
    case AxisScale::Time: return xVal;
    case AxisScale::LinearReverse: return xVal - xMax;
    default: return xVal - xMin;
    }
}

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
        return pixelPos;
    }

    ImVec2 pixOffset{plotLeft ? (-textSize.y + 2.0f) : (+5.0f), textSize.x};
    ImPlot::PlotText(label.data(), xData, yClamped, pixOffset, static_cast<int>(ImPlotTextFlags_Vertical) | static_cast<int>(ImPlotItemFlags_NoFit));
    return {pixelPos.x + pixOffset.x + textSize.y, pixelPos.y};
}

inline void drawStreamingTags(const SignalSink& sink, AxisScale axisScale, double xMin, double xMax, ImVec4 baseColor) {
    if (!sink.hasStreamingTags()) {
        return;
    }

    ImVec4 tagColor = baseColor;
    tagColor.w *= 0.35f;

    const float fontHeight  = ImGui::GetFontSize();
    const auto  plotLimits  = ImPlot::GetPlotLimits(IMPLOT_AUTO, IMPLOT_AUTO);
    const float yPixelRange = std::abs(ImPlot::PlotToPixels(0.0, plotLimits.Y.Max).y - ImPlot::PlotToPixels(0.0, plotLimits.Y.Min).y);

    ImGui::PushStyleColor(ImGuiCol_Text, tagColor);

    float lastTextPixelX = ImPlot::PlotToPixels(transformX(std::min(xMin, xMax), axisScale, xMin, xMax), 0.0).x;
    float lastAxisPixelX = ImPlot::PlotToPixels(transformX(std::max(xMin, xMax), axisScale, xMin, xMax), 0.0).x;

    sink.forEachTag([&](double timestamp, const gr::property_map& properties) {
        if (timestamp < std::min(xMin, xMax)) {
            return;
        }

        double      xTagPosition = transformX(timestamp, axisScale, xMin, xMax);
        const float xPixelPos    = ImPlot::PlotToPixels(xTagPosition, 0.0).x;

        ImPlot::SetNextLineStyle(tagColor);
        ImPlot::PlotInfLines("", &xTagPosition, 1, ImPlotInfLinesFlags_None);

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
            double      xTagPosition = xVal;
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

// ============================================================================
// Mouse Tooltip Utilities
// ============================================================================

namespace tooltip {

inline void formatAxisValue(const ImPlotAxis& axis, double value, char* buf, int size) {
    if (axis.Scale == ImPlotScale_Time) {
        const auto formatIsoTime = [](double timestamp, char* buffer, std::size_t size_) noexcept {
            using Clock          = std::chrono::system_clock;
            const auto timePoint = Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(timestamp)));
            const auto secs      = std::chrono::time_point_cast<std::chrono::seconds>(timePoint);
            const auto ms        = std::chrono::duration_cast<std::chrono::milliseconds>(timePoint - secs).count();

            auto result = std::format_to_n(buffer, static_cast<int>(size_) - 1, "{:%Y-%m-%dT%H:%M:%S}.{:03}", secs, ms);
            buffer[std::min(static_cast<std::size_t>(result.size), size_ - 1UZ)] = '\0';
        };
        formatIsoTime(value, buf, static_cast<std::size_t>(size));
    } else if (axis.Formatter) {
        axis.Formatter(value, buf, size, axis.FormatterData);
    } else {
        std::snprintf(buf, static_cast<std::size_t>(size), "%.6g", value);
    }
}

inline void showPlotMouseTooltip(double onDelay = 1.0, double offDelay = 30.0) {
    if (!ImPlot::IsPlotHovered()) {
        return;
    }

    ImPlotContext* ctx  = ImPlot::GetCurrentContext();
    ImPlotPlot*    plot = ImPlot::GetCurrentPlot();
    if (!ctx || !plot) {
        return;
    }

    const ImVec2  px = ImGui::GetMousePos();
    static ImVec2 lastPX{0, 0};
    static double lastTime = 0.0;
    const double  now      = ImGui::GetTime();

    constexpr float epsilon = 10.0f;
    const bool      samePos = std::abs(px.x - lastPX.x) < epsilon && std::abs(px.y - lastPX.y) < epsilon;

    if (!samePos) {
        lastPX   = px;
        lastTime = now;
        return;
    }

    if ((now - lastTime) < onDelay || (now - lastTime) > offDelay) {
        return;
    }

    auto drawAxisTooltip = [](ImPlotPlot* plot_, ImAxis axisIdx) {
        ImPlotAxis& axis = plot_->Axes[axisIdx];
        if (!axis.Enabled) {
            return;
        }

        const auto mousePos = ImPlot::GetPlotMousePos(axis.Vertical ? IMPLOT_AUTO : axisIdx, axis.Vertical ? axisIdx : IMPLOT_AUTO);

        char buf[128];
        formatAxisValue(axis, axis.Vertical ? mousePos.y : mousePos.x, buf, sizeof(buf));

        std::string_view label = plot_->GetAxisLabel(axis);
        if (label.empty()) {
            ImGui::Text("%s", buf);
        } else {
            ImGui::Text("%s: %s", label.data(), buf);
        }
    };

    {
        DigitizerUi::IMW::ToolTip tooltip;
        for (int i = 0; i < 3; ++i) {
            drawAxisTooltip(plot, ImAxis_X1 + i);
        }
        for (int i = 0; i < 3; ++i) {
            drawAxisTooltip(plot, ImAxis_Y1 + i);
        }
    }
}

} // namespace tooltip

// ============================================================================
// D&D Payload and Chart Mixin
// ============================================================================

/**
 * @brief D&D payload for signal sink transfers between charts/legend.
 */
struct DndPayload {
    char sinkName[256]     = {};
    char sourceChartId[64] = {};

    [[nodiscard]] bool hasSource() const noexcept { return sourceChartId[0] != '\0'; }
    [[nodiscard]] bool isValid() const noexcept { return sinkName[0] != '\0'; }
};

/**
 * @brief Non-polymorphic mixin providing signal sink storage and management.
 *
 * Concrete charts inherit from both gr::Block<Derived, ...> and Chart.
 */
struct Chart {
protected:
    std::vector<std::shared_ptr<SignalSink>> _signalSinks;

public:
    virtual ~Chart() = default;

    void addSignalSink(std::shared_ptr<SignalSink> sink) {
        if (sink) {
            _signalSinks.push_back(std::move(sink));
        }
    }

    void removeSignalSink(std::string_view name) {
        std::erase_if(_signalSinks, [&name](const auto& sink) { return sink && sink->name() == name; });
    }

    void clearSignalSinks() { _signalSinks.clear(); }

    [[nodiscard]] std::size_t                                      signalSinkCount() const noexcept { return _signalSinks.size(); }
    [[nodiscard]] const std::vector<std::shared_ptr<SignalSink>>&  signalSinks() const noexcept { return _signalSinks; }
    [[nodiscard]] std::vector<std::shared_ptr<SignalSink>>&        signalSinks() noexcept { return _signalSinks; }

    void syncSinksFromNames(const std::vector<std::string>& sinkNames) {
        std::set<std::string> desired(sinkNames.begin(), sinkNames.end());
        std::erase_if(_signalSinks, [&desired](const auto& sink) {
            return sink && desired.find(std::string(sink->name())) == desired.end() && desired.find(std::string(sink->signalName())) == desired.end();
        });

        std::set<std::string> current;
        for (const auto& sink : _signalSinks) {
            if (sink) {
                current.insert(std::string(sink->name()));
                current.insert(std::string(sink->signalName()));
            }
        }

        auto& registry = SinkRegistry::instance();
        for (const auto& name : sinkNames) {
            if (current.find(name) == current.end()) {
                // Search by both signalName (user-visible) and name (internal block name)
                if (auto sink = registry.findSink([&name](const auto& s) { return s.signalName() == name || s.name() == name; })) {
                    _signalSinks.push_back(sink);
                }
            }
        }
    }

    [[nodiscard]] std::vector<std::string> getSinkNames() const {
        std::vector<std::string> names;
        names.reserve(_signalSinks.size());
        for (const auto& sink : _signalSinks) {
            if (sink) {
                names.push_back(std::string(sink->name()));
            }
        }
        return names;
    }

    [[nodiscard]] virtual std::string_view uniqueId() const noexcept      = 0;
    [[nodiscard]] virtual std::string_view chartTypeName() const noexcept = 0;
};

/// @brief Callback to remove a sink from a chart by ID (used for D&D operations)
using RemoveSinkFromChartCallback = std::function<void(std::string_view chartId, std::string_view sinkName)>;
inline RemoveSinkFromChartCallback g_removeSinkFromChart = nullptr;

/**
 * @brief Callback for requesting chart type transmutation.
 *
 * Charts call this when the user selects a different chart type from the context menu.
 * DashboardPage sets this up to call Dashboard::transmuteChart().
 *
 * @param chartId The unique ID of the chart to transmute
 * @param newChartType The target chart type name (e.g., "YYChart")
 * @return true if transmutation succeeded
 */
using TransmuteChartCallback = std::function<bool(std::string_view chartId, std::string_view newChartType)>;
inline TransmuteChartCallback g_requestChartTransmutation = nullptr;

/**
 * @brief Common D&D helper methods for charts and signal legend.
 */
struct DndHelper {
    static constexpr const char* kPayloadType = "SIGNAL_SINK_DND";

    static bool handleDropTarget(Chart* chart, const char* payloadType = kPayloadType) {
        if (!chart) {
            return false;
        }
        bool dropped = false;
        if (ImPlot::BeginDragDropTargetPlot()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType)) {
                const auto* dnd = static_cast<const DndPayload*>(payload->Data);
                if (dnd && dnd->isValid()) {
                    dropped = processDrop(dnd, chart);
                }
            }
            ImPlot::EndDragDropTarget();
        }
        return dropped;
    }

    static bool handleLegendDropTarget(const char* payloadType = kPayloadType) {
        bool dropped = false;
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType)) {
                const auto* dnd = static_cast<const DndPayload*>(payload->Data);
                if (dnd && dnd->isValid() && dnd->hasSource()) {
                    removeFromSourceChart(dnd);
                    dropped = true;
                }
            }
            ImGui::EndDragDropTarget();
        }
        return dropped;
    }

    static void setupDragPayload(const std::shared_ptr<SignalSink>& sink, std::string_view sourceChartId, const char* payloadType = kPayloadType) {
        if (!sink) {
            return;
        }
        DndPayload dnd{};
        std::strncpy(dnd.sinkName, std::string(sink->name()).c_str(), sizeof(dnd.sinkName) - 1);
        if (!sourceChartId.empty()) {
            std::strncpy(dnd.sourceChartId, std::string(sourceChartId).c_str(), sizeof(dnd.sourceChartId) - 1);
        }
        ImGui::SetDragDropPayload(payloadType, &dnd, sizeof(dnd));
    }

    static void renderDragTooltip(const std::shared_ptr<SignalSink>& sink) {
        if (!sink) {
            return;
        }
        const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        const float  boxSize   = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddRectFilled(cursorPos, ImVec2(cursorPos.x + boxSize, cursorPos.y + boxSize), rgbToImGuiABGR(sink->color()));
        ImGui::Dummy(ImVec2(boxSize, boxSize));
        ImGui::SameLine();
        ImGui::TextUnformatted(std::string(sink->signalName()).c_str());
    }

private:
    static bool processDrop(const DndPayload* dnd, Chart* targetChart) {
        if (!dnd || !dnd->isValid() || !targetChart) {
            return false;
        }
        std::string sinkName(dnd->sinkName);
        auto        sinkSharedPtr = SinkRegistry::instance().findSink([&sinkName](const auto& sink) { return sink.name() == sinkName; });
        if (!sinkSharedPtr) {
            return false;
        }
        targetChart->addSignalSink(sinkSharedPtr);
        if (dnd->hasSource()) {
            removeFromSourceChart(dnd);
        }
        return true;
    }

    static void removeFromSourceChart(const DndPayload* dnd) {
        if (!dnd || !dnd->hasSource() || !g_removeSinkFromChart) {
            return;
        }
        g_removeSinkFromChart(dnd->sourceChartId, dnd->sinkName);
    }
};

} // namespace opendigitizer::charts

#endif // OPENDIGITIZER_CHARTS_CHART_HPP
