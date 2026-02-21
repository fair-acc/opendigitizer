#ifndef OPENDIGITIZER_CHARTS_CHART_HPP
#define OPENDIGITIZER_CHARTS_CHART_HPP

#include "SignalSink.hpp"
#include "SinkRegistry.hpp"

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

#include <imgui.h>
#include <implot.h>
#include <implot_internal.h>

#include "../common/TouchHandler.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/PmtTypeHelpers.hpp>
#include <gnuradio-4.0/Tag.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
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

namespace opendigitizer::charts {

enum class XAxisMode : std::uint8_t { UtcTime, RelativeTime, SampleIndex };

enum class AxisScale {
    Linear = 0,    // standard linear scale [min, max]
    LinearReverse, // reversed linear scale [max, min]
    Time,          // datetime/timestamp scale
    Log10,         // logarithmic base 10
    SymLog         // symmetric log (handles negative values)
};

enum class LabelFormat {
    Auto = 0,     // automatic based on range
    Metric,       // SI prefixes (k, M, G, etc.)
    MetricInline, // SI prefixes inline with value
    Scientific,   // scientific notation
    None,         // no labels
    Default       // default floating-point format
};

enum class HistoryUnit : int {
    seconds = 0, // time-based history depth (resolved via sample rate)
    samples = 1  // sample-count-based history depth
};

enum class AxisKind { X = 0, Y, Z };

[[nodiscard]] inline ImVec4 sinkColor(std::uint32_t rgb) { return ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(rgb)); }

struct AxisConfig {
    AxisKind                 axis = AxisKind::X;
    float                    min  = std::numeric_limits<float>::quiet_NaN();
    float                    max  = std::numeric_limits<float>::quiet_NaN();
    std::optional<AxisScale> scale;
    LabelFormat              format   = LabelFormat::Auto;
    float                    width    = std::numeric_limits<float>::max();
    bool                     plotTags = true;
};

[[nodiscard]] inline std::optional<AxisConfig> parseAxisConfig(const gr::property_map& constraints, AxisKind targetKind, std::size_t index = 0) {
    auto axesIt = constraints.find("axes");
    if (axesIt == constraints.end()) {
        return std::nullopt;
    }
    const auto* axesVec = axesIt->second.get_if<gr::Tensor<gr::pmt::Value>>();
    if (axesVec == nullptr) {
        return std::nullopt;
    }

    std::size_t count = 0;
    for (const auto& axisPmt : *axesVec) {
        const auto* axisMap = axisPmt.get_if<gr::property_map>();
        if (axisMap == nullptr) {
            continue;
        }
        auto axisStrIt = axisMap->find("axis");
        if (axisStrIt == axisMap->end()) {
            continue;
        }
        if (!axisStrIt->second.is_string()) {
            continue;
        }
        const auto axisStr    = axisStrIt->second.value_or(std::string_view{});
        AxisKind   parsedKind = AxisKind::Y;
        if (axisStr == "X" || axisStr == "x") {
            parsedKind = AxisKind::X;
        } else if (axisStr == "Z" || axisStr == "z") {
            parsedKind = AxisKind::Z;
        }
        if (parsedKind != targetKind) {
            continue;
        }
        if (count != index) {
            ++count;
            continue;
        }

        AxisConfig cfg;
        cfg.axis = parsedKind;
        if (auto it = axisMap->find("min"); it != axisMap->end()) {
            cfg.min = it->second.value_or(std::numeric_limits<float>::quiet_NaN());
        }
        if (auto it = axisMap->find("max"); it != axisMap->end()) {
            cfg.max = it->second.value_or(std::numeric_limits<float>::quiet_NaN());
        }
        if (auto it = axisMap->find("scale"); it != axisMap->end()) {
            if (it->second.is_string()) {
                cfg.scale = magic_enum::enum_cast<AxisScale>(it->second.value_or(std::string()), magic_enum::case_insensitive).value_or(AxisScale::Linear);
            }
        }
        if (auto it = axisMap->find("format"); it != axisMap->end()) {
            if (it->second.is_string()) {
                cfg.format = magic_enum::enum_cast<LabelFormat>(it->second.value_or(std::string()), magic_enum::case_insensitive).value_or(LabelFormat::Auto);
            }
        }
        if (auto it = axisMap->find("plot_tags"); it != axisMap->end()) {
            cfg.plotTags = it->second.value_or(true);
        }
        return cfg;
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<AxisConfig> parseAxisConfig(const gr::property_map& constraints, bool isX, std::size_t index = 0) { return parseAxisConfig(constraints, isX ? AxisKind::X : AxisKind::Y, index); }

[[nodiscard]] inline std::pair<double, double> effectiveColourRange(const gr::property_map& uiConstraints, double autoScaleMin, double autoScaleMax) {
    auto zCfg = parseAxisConfig(uiConstraints, AxisKind::Z);
    if (zCfg) {
        bool autoMin = !std::isfinite(zCfg->min);
        bool autoMax = !std::isfinite(zCfg->max);
        if (!autoMin && !autoMax) {
            return {static_cast<double>(zCfg->min), static_cast<double>(zCfg->max)};
        }
        double cMin = autoMin ? autoScaleMin : static_cast<double>(zCfg->min);
        double cMax = autoMax ? autoScaleMax : static_cast<double>(zCfg->max);
        return {cMin, cMax};
    }
    return {autoScaleMin, autoScaleMax};
}

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
        return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::ptrdiff_t>(size), "0{}", unit));
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

    return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::ptrdiff_t>(size), "{:g}{}{}", scaledValue, prefix, unit));
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
        return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::ptrdiff_t>(size), "0{}", unit));
    }
    const double absVal = std::abs(value);
    if (absVal >= 1e-3 && absVal < 10000.0) {
        return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::ptrdiff_t>(size), "{:.3g}{}", value, unit));
    } else {
        return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::ptrdiff_t>(size), "{}{}", formatMinimalScientific(value), unit));
    }
}

inline int formatDefault(double value, char* buff, int size, void* data) {
    constexpr std::size_t  maxUnitLength = 10UZ;
    const std::string_view unit          = boundedStringView(static_cast<const char*>(data), maxUnitLength);

    if (value == 0.0) {
        return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::ptrdiff_t>(size), "0{}", unit));
    }
    return enforceNullTerminate(buff, size, std::format_to_n(buff, static_cast<std::ptrdiff_t>(size), "{:g}{}", value, unit));
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

inline void setupAxis(ImAxis axisId, const std::optional<AxisCategory>& category, LabelFormat format, float axisWidth, double minLimit, double maxLimit, std::size_t nTotalAxes, AxisScale scaleOverride, std::array<std::string, 6>& unitStringStorage, bool showGrid = true, bool foreground = false, ImPlotCond limitsCond = ImPlotCond_Always) {
    if (!category.has_value()) {
        return;
    }

    const bool      isX       = axisId == ImAxis_X1 || axisId == ImAxis_X2 || axisId == ImAxis_X3;
    const bool      finiteMin = std::isfinite(minLimit);
    const bool      finiteMax = std::isfinite(maxLimit);
    const AxisScale scale     = scaleOverride;

    ImPlotAxisFlags flags = showGrid ? ImPlotAxisFlags_None : ImPlotAxisFlags_NoGridLines;
    if (foreground) {
        flags |= ImPlotAxisFlags_Foreground;
    }
    if (finiteMin && !finiteMax) {
        flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_LockMin;
    } else if (!finiteMin && finiteMax) {
        flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_LockMax;
    } else if (!finiteMin && !finiteMax) {
        flags |= ImPlotAxisFlags_AutoFit;
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

    if (format == LabelFormat::None) {
        flags |= ImPlotAxisFlags_NoTickLabels;
    }

    const std::string label = (scale == AxisScale::Time) || (format == LabelFormat::MetricInline) || (format == LabelFormat::None) ? "" : truncateLabel(category->buildLabel(), axisWidth);
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
            unitStringStorage[static_cast<std::size_t>(axisId)] = unit;
            ImPlot::SetupAxisFormat(axisId, formatMetric, const_cast<void*>(static_cast<const void*>(unitStringStorage[static_cast<std::size_t>(axisId)].c_str())));
        } break;
        case LabelFormat::Scientific: ImPlot::SetupAxisFormat(axisId, formatScientific, nullptr); break;
        case LabelFormat::None: break; // tick labels suppressed via ImPlotAxisFlags_NoTickLabels
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
        ImPlot::SetupAxisLimits(axisId, minLimit, maxLimit, limitsCond);
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
    // Find existing category with matching quantity/unit
    auto existingIt = std::ranges::find_if(categories, [&](const auto& cat) { return cat.has_value() && cat->matches(quantity, unit); });
    if (existingIt != categories.end()) {
        return static_cast<std::size_t>(std::distance(categories.begin(), existingIt));
    }

    // Find first empty slot and create new category
    auto emptyIt = std::ranges::find_if(categories, [](const auto& cat) { return !cat.has_value(); });
    if (emptyIt != categories.end()) {
        *emptyIt = AxisCategory{.quantity = std::string(quantity), .unit = std::string(unit), .color = color};
        return static_cast<std::size_t>(std::distance(categories.begin(), emptyIt));
    }

    return std::nullopt;
}

inline void buildAxisCategories(const std::vector<std::shared_ptr<SignalSink>>& signalSinks, std::array<std::optional<AxisCategory>, 3>& xCategories, std::array<std::optional<AxisCategory>, 3>& yCategories, std::array<std::vector<std::string>, 3>& xAxisGroups, std::array<std::vector<std::string>, 3>& yAxisGroups) {
    std::ranges::for_each(xCategories, [](auto& cat) { cat.reset(); });
    std::ranges::for_each(yCategories, [](auto& cat) { cat.reset(); });
    std::ranges::for_each(xAxisGroups, [](auto& group) { group.clear(); });
    std::ranges::for_each(yAxisGroups, [](auto& group) { group.clear(); });

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

inline std::size_t findAxisForSink(std::string_view sinkName, bool isX, const std::array<std::vector<std::string>, 3>& xAxisGroups, const std::array<std::vector<std::string>, 3>& yAxisGroups) {
    const auto& groups  = isX ? xAxisGroups : yAxisGroups;
    auto        foundIt = std::ranges::find_if(groups, [sinkName](const auto& group) { return std::ranges::contains(group, sinkName); });
    return foundIt != groups.end() ? static_cast<std::size_t>(std::distance(groups.begin(), foundIt)) : 0;
}

inline std::size_t activeAxisCount(const std::array<std::optional<AxisCategory>, 3>& categories) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(categories, [](const auto& c) { return c.has_value(); }));
}

} // namespace axis

namespace tags {

/// Marker key for tags that appear out-of-order or have suspicious timestamps.
inline constexpr std::string_view kFishyTagKey = "ui_fishy_tag";

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

template<typename ForEachTagFn>
inline void drawTags(ForEachTagFn&& forEachTagFn, AxisScale axisScale, double xMin, double xMax, ImVec4 tagColor) {
    DigitizerUi::IMW::Font titleFont(DigitizerUi::LookAndFeel::instance().fontTiny[DigitizerUi::LookAndFeel::instance().prototypeMode]);

    const float fontHeight  = ImGui::GetFontSize();
    const auto  plotLimits  = ImPlot::GetPlotLimits(IMPLOT_AUTO, IMPLOT_AUTO);
    const float yPixelRange = std::abs(ImPlot::PlotToPixels(0.0, plotLimits.Y.Max).y - ImPlot::PlotToPixels(0.0, plotLimits.Y.Min).y);

    ImGui::PushStyleColor(ImGuiCol_Text, tagColor);

    float lastTextPixelX = ImPlot::PlotToPixels(transformX(std::min(xMin, xMax), axisScale, xMin, xMax), 0.0).x;
    float lastAxisPixelX = ImPlot::PlotToPixels(transformX(std::max(xMin, xMax), axisScale, xMin, xMax), 0.0).x;

    forEachTagFn([&](double timestamp, const gr::property_map& properties) {
        if (timestamp < std::min(xMin, xMax) || timestamp > std::max(xMin, xMax)) {
            return;
        }

        double      xTagPosition = transformX(timestamp, axisScale, xMin, xMax);
        const float xPixelPos    = ImPlot::PlotToPixels(xTagPosition, 0.0).x;

        // highlight fishy tags (out-of-order timestamps) in magenta
        if (properties.contains(std::string(kFishyTagKey))) {
            ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.0f, 1.0f, 1.0f));
        } else {
            ImPlot::SetNextLineStyle(tagColor);
        }
        ImPlot::PlotInfLines("", &xTagPosition, 1, ImPlotInfLinesFlags_None);

        // suppress tag labels if too close to previous or to axis extremities
        if ((xPixelPos - lastTextPixelX) > 1.5f * fontHeight && (lastAxisPixelX - xPixelPos) > 2.0f * fontHeight) {
            std::string triggerLabel = "TRIGGER";
            if (const auto it = properties.find(std::string(gr::tag::TRIGGER_NAME.shortKey())); it != properties.end()) {
                if (const auto str = it->second.value_or(std::string_view{}); str.data() != nullptr) {
                    triggerLabel = str;
                }
            }

            const ImVec2 triggerLabelSize = ImGui::CalcTextSize(triggerLabel.c_str());
            if (triggerLabelSize.x < 0.75f * yPixelRange) {
                lastTextPixelX = plotVerticalTagLabel(triggerLabel, xTagPosition, plotLimits, true).x;

                std::string triggerCtx;
                if (const auto it = properties.find(std::string(gr::tag::CONTEXT.shortKey())); it != properties.end()) {
                    if (const auto str = it->second.value_or(std::string_view{}); str.data() != nullptr) {
                        triggerCtx = str;
                    }
                }
                if (!triggerCtx.empty() && triggerCtx != triggerLabel) {
                    const ImVec2 ctxLabelSize = ImGui::CalcTextSize(triggerCtx.c_str());
                    if (ctxLabelSize.x < 0.75f * yPixelRange) {
                        lastTextPixelX = plotVerticalTagLabel(triggerCtx, xTagPosition, plotLimits, false).x;
                    }
                }
            }
        }
    });

    ImGui::PopStyleColor();
}

template<typename T>
inline void drawDataSetTimingEvents(const gr::DataSet<T>& dataSet, AxisScale axisScale, ImVec4 baseColor) {
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

    float lastTextPixelX = ImPlot::PlotToPixels(transformX(std::min(xMin, xMax), axisScale, xMin, xMax, true), 0.0).x;
    float lastAxisPixelX = ImPlot::PlotToPixels(transformX(std::max(xMin, xMax), axisScale, xMin, xMax, true), 0.0).x;

    for (const auto& eventsForSig : dataSet.timing_events) {
        for (const auto& [xIndex, tagMap] : eventsForSig) {
            if (xIndex < 0 || static_cast<std::size_t>(xIndex) >= xAxisSpan.size()) {
                continue;
            }

            double      xVal         = static_cast<double>(xAxisSpan[static_cast<std::size_t>(xIndex)]);
            double      xTagPosition = transformX(xVal, axisScale, xMin, xMax, true);
            const float xPixelPos    = ImPlot::PlotToPixels(xTagPosition, 0.0).x;

            ImPlot::SetNextLineStyle(tagColor);
            ImPlot::PlotInfLines("", &xTagPosition, 1, ImPlotInfLinesFlags_None);

            if ((xPixelPos - lastTextPixelX) > 1.5f * fontHeight && (lastAxisPixelX - xPixelPos) > 2.0f * fontHeight) {
                std::string triggerLabel = "TRIGGER";
                if (auto it = tagMap.find(std::string(gr::tag::TRIGGER_NAME.shortKey())); it != tagMap.end()) {
                    if (const auto str = it->second.value_or(std::string_view{}); str.data() != nullptr) {
                        triggerLabel = str;
                    }
                }

                const ImVec2 triggerLabelSize = ImGui::CalcTextSize(triggerLabel.c_str());
                if (triggerLabelSize.x < 0.75f * yPixelRange) {
                    lastTextPixelX = plotVerticalTagLabel(triggerLabel, xTagPosition, plotLimits, true).x;
                } else {
                    continue;
                }

                // Render CONTEXT tag label below trigger label if present and different
                std::string triggerCtx;
                if (const auto it = tagMap.find(std::string(gr::tag::CONTEXT.shortKey())); it != tagMap.end()) {
                    if (const auto str = it->second.value_or(std::string_view{}); str.data() != nullptr) {
                        triggerCtx = str;
                    }
                }
                if (!triggerCtx.empty() && triggerCtx != triggerLabel) {
                    const ImVec2 triggerCtxLabelSize = ImGui::CalcTextSize(triggerCtx.c_str());
                    if (triggerCtxLabelSize.x < 0.75f * yPixelRange) {
                        lastTextPixelX = plotVerticalTagLabel(triggerCtx, xTagPosition, plotLimits, false).x;
                    }
                }
            }
        }
    }

    ImGui::PopStyleColor();
}

} // namespace tags

namespace tooltip {

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

    // Format axis value according to axis scale/formatter
    const auto formatAxisValue = [](const ImPlotAxis& axis, double value, char* buf, int size) {
        if (axis.Scale == ImPlotScale_Time) {
            using Clock                                                                                = std::chrono::system_clock;
            const auto timePoint                                                                       = Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(value)));
            const auto secs                                                                            = std::chrono::time_point_cast<std::chrono::seconds>(timePoint);
            const auto ms                                                                              = std::chrono::duration_cast<std::chrono::milliseconds>(timePoint - secs).count();
            auto       result                                                                          = std::format_to_n(buf, static_cast<int>(size) - 1, "{:%Y-%m-%dT%H:%M:%S}.{:03}", secs, ms);
            buf[std::min(static_cast<std::size_t>(result.size), static_cast<std::size_t>(size) - 1UZ)] = '\0';
        } else if (axis.Formatter) {
            axis.Formatter(value, buf, size, axis.FormatterData);
        } else {
            auto result = std::format_to_n(buf, static_cast<std::ptrdiff_t>(size) - 1, "{:.6g}", value);
            *result.out = '\0';
        }
    };

    auto drawAxisTooltip = [&formatAxisValue](ImPlotPlot* plot_, ImAxis axisIdx) {
        if (axisIdx < 0 || axisIdx >= ImAxis_COUNT) {
            return;
        }
        ImPlotAxis& axis = plot_->Axes[axisIdx];
        if (!axis.Enabled || !axis.HasRange) {
            return;
        }

        const auto mousePos = ImPlot::GetPlotMousePos(axis.Vertical ? IMPLOT_AUTO : axisIdx, axis.Vertical ? axisIdx : IMPLOT_AUTO);

        char buf[128];
        formatAxisValue(axis, axis.Vertical ? mousePos.y : mousePos.x, buf, sizeof(buf));
        ImGui::Text("%s", buf);
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

inline constexpr std::string_view kDefaultChartType = "opendigitizer::charts::XYChart";

inline std::vector<std::string> registeredChartTypes() {
    std::vector<std::string> chartTypes;
    for (const auto& blockName : gr::globalBlockRegistry().keys()) {
        // Case-insensitive check for "chart" in the block name
        std::string lowerName = blockName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lowerName.find("chart") != std::string::npos && lowerName.find("chartmonitor") == std::string::npos) {
            chartTypes.push_back(blockName);
        }
    }
    std::ranges::sort(chartTypes);
    return chartTypes;
}

/// D&D protocol for signal sink transfers between charts and legend.
namespace dnd {

constexpr const char* kPayloadType = "SIGNAL_SINK_DND";

template<std::size_t N>
constexpr void copyToBuffer(char (&dest)[N], std::string_view src) noexcept {
    const auto count = std::min(src.size(), N - 1);
    std::copy(src.data(), src.data() + count, dest);
    dest[count] = '\0';
}

struct Payload {
    char sink_name[256]      = {};
    char source_chart_id[64] = {};

    [[nodiscard]] bool hasSource() const noexcept { return source_chart_id[0] != '\0'; }
    [[nodiscard]] bool isValid() const noexcept { return sink_name[0] != '\0'; }
};

struct State {
    bool        accepted = false;
    std::string source_chart_id;
    std::string sink_name;

    void reset() {
        accepted = false;
        source_chart_id.clear();
        sink_name.clear();
    }

    [[nodiscard]] bool isAcceptedFrom(std::string_view chartId) const { return accepted && source_chart_id == chartId; }
};
inline State g_state;

inline bool handleLegendDropTarget(const char* payloadType = kPayloadType) {
    bool dropped = false;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType)) {
            const auto* dnd = static_cast<const Payload*>(payload->Data);
            if (dnd && dnd->isValid() && dnd->hasSource()) {
                g_state.accepted = true;
                dropped          = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    return dropped;
}

inline void setupPayload(const std::shared_ptr<SignalSink>& sink, std::string_view sourceChartId, const char* payloadType = kPayloadType) {
    if (!sink) {
        return;
    }
    Payload           dnd{};
    const std::string sinkIdentifier = sink->signalName().empty() ? std::string(sink->name()) : std::string(sink->signalName());
    dnd::copyToBuffer(dnd.sink_name, sinkIdentifier);
    if (!sourceChartId.empty()) {
        dnd::copyToBuffer(dnd.source_chart_id, sourceChartId);
    }
    ImGui::SetDragDropPayload(payloadType, &dnd, sizeof(dnd));

    g_state.accepted        = false;
    g_state.source_chart_id = sourceChartId;
    g_state.sink_name       = sinkIdentifier;
}

inline void renderDragTooltip(const std::shared_ptr<SignalSink>& sink) {
    if (!sink) {
        return;
    }
    const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    const float  boxSize   = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(cursorPos, ImVec2(cursorPos.x + boxSize, cursorPos.y + boxSize), rgbToImGuiABGR(sink->color()));
    ImGui::Dummy(ImVec2(boxSize, boxSize));
    ImGui::SameLine();
    const auto signalName = sink->signalName();
    ImGui::TextUnformatted(signalName.data(), signalName.data() + signalName.size());
}

using AddSinkToChartCallback                   = std::function<void(std::string_view chartId, std::string_view sinkName)>;
inline AddSinkToChartCallback g_addSinkToChart = nullptr;

} // namespace dnd

/// Callback for requesting chart type transmutation.
using TransmuteChartCallback                              = std::function<bool(std::string_view chartId, std::string_view newChartType)>;
inline TransmuteChartCallback g_requestChartTransmutation = nullptr;

/// Callback for requesting chart duplication.
using DuplicateChartCallback                            = std::function<void(std::string_view chartId)>;
inline DuplicateChartCallback g_requestChartDuplication = nullptr;

/// Callback for requesting chart removal.
using RemoveChartCallback                        = std::function<void(std::string_view chartId)>;
inline RemoveChartCallback g_requestChartRemoval = nullptr;

/// Font Awesome icon constants for context menus.
namespace menu_icons {
inline constexpr const char* kXAxis      = "\uf547"; // ruler-horizontal
inline constexpr const char* kYAxis      = "\uf548"; // ruler-vertical
inline constexpr const char* kSettings   = "\uf013"; // gear
inline constexpr const char* kMore       = "\uf141"; // ellipsis
inline constexpr const char* kChangeType = "\uf0ec"; // arrows-rotate
inline constexpr const char* kDuplicate  = "\uf0c5"; // copy
inline constexpr const char* kRemove     = "\uf2ed"; // trash-can
inline constexpr const char* kAutoFit    = "\uf0b2"; // arrows-maximize
inline constexpr const char* kLegend     = "\uf0ca"; // list-ul
inline constexpr const char* kTags       = "\uf02b"; // tag
inline constexpr const char* kGrid       = "\uf00a"; // table-cells
inline constexpr const char* kAntiAlias  = "\uf7d9"; // wave-square
inline constexpr const char* kScale      = "\uf545"; // ruler-combined
inline constexpr const char* kMin        = "\uf068"; // minus
inline constexpr const char* kMax        = "\uf067"; // plus
inline constexpr const char* kCheckOn    = "\uf14a"; // square-check
inline constexpr const char* kCheckOff   = "\uf0c8"; // square
inline constexpr const char* kHistory    = "\uf1da"; // clock-rotate-left
inline constexpr const char* kArrow      = "\uf061"; // arrow-right
inline constexpr const char* kFormat     = "\uf031"; // font

/// Renders icon followed by text, using fontIconsSolid for the icon portion.
inline void iconText(const char* icon, const char* text) {
    {
        DigitizerUi::IMW::Font iconFont(DigitizerUi::LookAndFeel::instance().fontIconsSolid);
        ImGui::TextUnformatted(icon);
    }
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(text);
}

/// Creates a menu item label with icon prefix (icon rendered in fontIconsSolid).
/// Returns the combined label string for use with ImGui::MenuItem or ImGui::BeginMenu.
inline std::string makeIconLabel(const char* icon, std::string_view text) { return std::format("{} {}", icon, text); }

/// MenuItem with icon (icon rendered with fontIconsSolid font).
inline bool menuItemWithIcon(const char* icon, const char* label, bool selected = false, bool enabled = true) {
    bool clicked = false;
    {
        DigitizerUi::IMW::Font iconFont(DigitizerUi::LookAndFeel::instance().fontIconsSolid);
        ImGui::TextUnformatted(icon);
    }
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    if (ImGui::MenuItem(label, nullptr, selected, enabled)) {
        clicked = true;
    }
    return clicked;
}

/// BeginMenu with icon (icon rendered with fontIconsSolid font).
/// Returns true if menu is open - caller must call ImGui::EndMenu() when done.
inline bool beginMenuWithIcon(const char* icon, const char* label, bool enabled = true) {
    // Draw icon with icon font
    {
        DigitizerUi::IMW::Font iconFont(DigitizerUi::LookAndFeel::instance().fontIconsSolid);
        ImGui::TextUnformatted(icon);
    }
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    // BeginMenu handles its own ID - don't add extra PushID/PopID
    return ImGui::BeginMenu(label, enabled);
}

} // namespace menu_icons

struct DrawPrologue {
    ImPlotFlags plotFlags;
    ImVec2      plotSize;
    bool        showLegend;
    bool        layoutMode;
    bool        showGrid;
};

namespace detail {

inline constexpr bool isExcludedFromAutoSettings(std::string_view name) {
    using namespace std::string_view_literals;
    constexpr std::array kExcluded = {
        "chart_name"sv,
        "chart_title"sv,
        "data_sinks"sv,
        "x_min"sv,
        "x_max"sv,
        "y_min"sv,
        "y_max"sv,
        "x_auto_scale"sv,
        "y_auto_scale"sv,
        "n_history"sv,
        "x_axis_mode"sv,
        "x_axis_scale"sv,
        "y_axis_scale"sv,
        "input_chunk_size"sv,
        "output_chunk_size"sv,
        "stride"sv,
        "disconnect_on_done"sv,
        "compute_domain"sv,
        "unique_name"sv,
        "name"sv,
        "ui_constraints"sv,
    };
    return std::ranges::contains(kExcluded, name);
}

inline void onScrollWheel(auto&& apply) {
    if (ImGui::IsItemHovered()) {
        if (float wheel = ImGui::GetIO().MouseWheel; wheel != 0.0f) {
            apply(wheel);
        }
    }
}

} // namespace detail

/// Non-polymorphic CRTP mixin for chart signal sink storage and D&D using C++23 deducing this.
/// Derived classes must: inherit gr::Block<Derived>, define kChartTypeName, data_sinks.
struct Chart {
    static constexpr std::size_t kDefaultHistorySize             = 4096;
    static constexpr double      kCapacityRefreshIntervalSeconds = 30.0; // refresh before 60s timeout
    static constexpr double      kCapacityDebounceSeconds        = 0.3;  // debounce resize to avoid discontinuities

    // helpers that abstract over scalar (bool / double) vs per-axis array (std::array<T,3>) properties
    template<typename Field>
    static bool readAutoScale(const Field& field, std::size_t idx) {
        if constexpr (requires { field.value[0]; }) {
            return field.value[idx];
        } else {
            return idx == 0 ? static_cast<bool>(field.value) : true;
        }
    }
    template<typename Field>
    static void writeAutoScale(Field& field, std::size_t idx, bool val) {
        if constexpr (requires { field.value[0]; }) {
            field.value[idx] = val;
        } else if (idx == 0) {
            field = val;
        }
    }
    template<typename Field>
    static double readLimit(const Field& field, std::size_t idx) {
        if constexpr (requires { field.value[0]; }) {
            return field.value[idx];
        } else {
            return idx == 0 ? static_cast<double>(field.value) : std::numeric_limits<double>::quiet_NaN();
        }
    }
    template<typename Field>
    static void writeLimit(Field& field, std::size_t idx, double val) {
        if constexpr (requires { field.value[0]; }) {
            field.value[idx] = val;
        } else if (idx == 0) {
            field = val;
        }
    }

    std::vector<std::shared_ptr<SignalSink>> _signalSinks;
    double                                   _lastCapacityRefreshTime = 0.0;
    double                                   _pendingResizeTime       = 0.0;                  // 0 = no pending resize
    HistoryUnit                              _historyDisplayUnit      = HistoryUnit::seconds; // UI-only, not persisted
    std::optional<ImAxis>                    _hoveredAxisForMenu;                             // specific axis ID for right-click popup
    std::array<int, 3>                       _fitOnceX            = {};                       // per-axis: 0=idle, 2=auto-fit pending, 1=capture next frame
    std::array<int, 3>                       _fitOnceY            = {};
    std::array<double, 3>                    _prevXMin            = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    std::array<double, 3>                    _prevXMax            = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    std::array<double, 3>                    _prevYMin            = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    std::array<double, 3>                    _prevYMax            = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    std::array<bool, 3>                      _limitsForceAppliedX = {}; // true on frame where limits were force-applied
    std::array<bool, 3>                      _limitsForceAppliedY = {};

    ImPlotCond trackLimitsCond(bool isX, double newMin, double newMax, std::size_t axisIdx = 0) {
        auto& prevMin   = isX ? _prevXMin[axisIdx] : _prevYMin[axisIdx];
        auto& prevMax   = isX ? _prevXMax[axisIdx] : _prevYMax[axisIdx];
        auto& forceFlag = isX ? _limitsForceAppliedX[axisIdx] : _limitsForceAppliedY[axisIdx];
        bool  changed   = (newMin != prevMin || newMax != prevMax);
        prevMin         = newMin;
        prevMax         = newMax;
        forceFlag       = changed;
        return changed ? ImPlotCond_Always : ImPlotCond_Once;
    }

    template<typename Self>
    void addSignalSink(this Self& self, std::shared_ptr<SignalSink> sink) {
        if (!sink) {
            return;
        }
        auto it = std::find(self._signalSinks.begin(), self._signalSinks.end(), sink);
        if (it == self._signalSinks.end()) {
            std::size_t capacity = kDefaultHistorySize;
            if constexpr (requires { self.n_history.value; }) {
                capacity = static_cast<std::size_t>(self.n_history.value);
            }
            sink->requestCapacity(std::string(self.unique_name), capacity);
            self._signalSinks.push_back(std::move(sink));
        }
    }

    void removeSignalSink(std::string_view name) {
        std::erase_if(_signalSinks, [&name](const auto& sink) { return sink && (sink->name() == name || sink->signalName() == name); });
    }

    void clearSignalSinks() { _signalSinks.clear(); }

    [[nodiscard]] std::size_t                                     signalSinkCount() const noexcept { return _signalSinks.size(); }
    [[nodiscard]] const std::vector<std::shared_ptr<SignalSink>>& signalSinks() const noexcept { return _signalSinks; }
    [[nodiscard]] std::vector<std::shared_ptr<SignalSink>>&       signalSinks() noexcept { return _signalSinks; }

    [[nodiscard]] std::pair<std::string, std::string> sinkAxisInfo(bool isX) const {
        for (const auto& sink : _signalSinks) {
            auto quantity = isX ? sink->abscissaQuantity() : sink->signalQuantity();
            auto unit     = isX ? sink->abscissaUnit() : sink->signalUnit();
            if (!quantity.empty() || !unit.empty()) {
                return {std::string(quantity), std::string(unit)};
            }
        }
        return isX ? std::pair{std::string("Frequency"), std::string("Hz")} : std::pair{std::string("magnitude"), std::string("dB")};
    }

    void syncSinksFromNames(const std::vector<std::string>& sinkNames) {
        std::set<std::string> desired(sinkNames.begin(), sinkNames.end());
        std::erase_if(_signalSinks, [&desired](const auto& sink) { return sink && !desired.contains(std::string(sink->name())) && !desired.contains(std::string(sink->signalName())); });

        std::set<std::string> current;
        for (const auto& sink : _signalSinks) {
            if (sink) {
                current.insert(std::string(sink->name()));
                current.insert(std::string(sink->signalName()));
            }
        }

        auto& registry = SinkRegistry::instance();
        for (const auto& name : sinkNames) {
            if (!current.contains(name)) {
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

    template<typename Self>
    void onDataSinksChanged(this Self& self, const std::vector<std::string>& sinkNames) {
        self.syncSinksFromNames(sinkNames);
        std::size_t capacity = kDefaultHistorySize;
        if constexpr (requires { self.n_history.value; }) {
            capacity = static_cast<std::size_t>(self.n_history.value);
        }
        for (auto& sink : self._signalSinks) {
            if (sink) {
                sink->requestCapacity(std::string(self.unique_name), capacity);
            }
        }
    }

    void syncSinksIfNeeded(const std::vector<std::string>& dataSinks) {
        if (_signalSinks.size() != dataSinks.size()) {
            syncSinksFromNames(dataSinks);
            return;
        }
        for (std::size_t i = 0; i < dataSinks.size(); ++i) {
            if (!_signalSinks[i] || (_signalSinks[i]->name() != dataSinks[i] && _signalSinks[i]->signalName() != dataSinks[i])) {
                syncSinksFromNames(dataSinks);
                return;
            }
        }
    }

    template<typename Self>
    void onSinkRemovedFromDnd(this Self& self, std::string_view sinkName) {
        // resolve the canonical block name for the sink being removed, since
        // data_sinks may store either name() or signalName()
        std::string blockName;
        for (const auto& sink : self._signalSinks) {
            if (sink && (sink->signalName() == sinkName || sink->name() == sinkName)) {
                blockName = std::string(sink->name());
                break;
            }
        }
        // erase by both the DnD name and the resolved block name
        std::erase_if(self.data_sinks.value, [&sinkName, &blockName](const std::string& entry) { return entry == sinkName || (!blockName.empty() && entry == blockName); });
        gr::Tensor<gr::pmt::Value> sinks(gr::extents_from, {self.data_sinks.value.size()});
        for (std::size_t i = 0; i < self.data_sinks.value.size(); ++i) {
            sinks[i] = self.data_sinks.value[i];
        }
        std::ignore = self.settings().set(gr::property_map{{std::pmr::string("data_sinks"), std::move(sinks)}});
        std::ignore = self.settings().applyStagedParameters();
    }

    template<typename Self>
    void onSinkAddedFromDnd(this Self& self, std::string_view sinkName, std::shared_ptr<SignalSink> sink) {
        // normalise to block name for consistent data_sinks storage
        std::string canonicalName = sink ? std::string(sink->name()) : std::string(sinkName);
        if (std::find(self.data_sinks.value.begin(), self.data_sinks.value.end(), canonicalName) == self.data_sinks.value.end()) {
            self.data_sinks.value.push_back(canonicalName);
            gr::Tensor<gr::pmt::Value> sinks(gr::extents_from, {self.data_sinks.value.size()});
            for (std::size_t i = 0; i < self.data_sinks.value.size(); ++i) {
                sinks[i] = self.data_sinks.value[i];
            }
            std::ignore = self.settings().set(gr::property_map{{std::pmr::string("data_sinks"), std::move(sinks)}});
            std::ignore = self.settings().applyStagedParameters();
        }
        self.addSignalSink(std::move(sink));
    }

    template<typename Self>
    void processAcceptedDndRemoval(this Self& self) {
        if (dnd::g_state.isAcceptedFrom(self.unique_name)) {
            self.onSinkRemovedFromDnd(dnd::g_state.sink_name);
            self.removeSignalSink(dnd::g_state.sink_name);
            dnd::g_state.reset();
        }
    }

    template<typename Self>
    void setupLegendDragSources(this Self& self) {
        for (const auto& sink : self._signalSinks) {
            std::string signalNameStr(sink->signalName());
            if (signalNameStr.empty()) {
                signalNameStr = std::string(sink->name());
            }
            if (ImPlot::BeginDragDropSourceItem(signalNameStr.c_str())) {
                dnd::setupPayload(sink, self.unique_name);
                dnd::renderDragTooltip(sink);
                ImPlot::EndDragDropSource();
            }
        }
    }

    template<typename Self>
    bool handlePlotDropTarget(this Self& self, const char* payloadType = dnd::kPayloadType) {
        bool dropped = false;
        if (ImPlot::BeginDragDropTargetPlot()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType)) {
                const auto* dndPayload = static_cast<const dnd::Payload*>(payload->Data);
                if (dndPayload && dndPayload->isValid()) {
                    std::string sinkName(dndPayload->sink_name);
                    auto        sinkSharedPtr = SinkRegistry::instance().findSink([&sinkName](const auto& s) { return s.signalName() == sinkName || s.name() == sinkName; });
                    if (sinkSharedPtr) {
                        self.onSinkAddedFromDnd(sinkName, sinkSharedPtr);
                        if (dndPayload->hasSource()) {
                            dnd::g_state.accepted = true;
                        }
                        dropped = true;
                    }
                }
            }
            ImPlot::EndDragDropTarget();
        }
        return dropped;
    }

    template<typename Self>
    void drawChartTypeSubmenu(this Self& self) {
        if (menu_icons::beginMenuWithIcon(menu_icons::kChangeType, "Change Type")) {
            for (const auto& type : registeredChartTypes()) {
                bool isCurrent = type.ends_with(std::remove_cvref_t<Self>::kChartTypeName);
                if (ImGui::MenuItem(type.c_str(), nullptr, isCurrent)) {
                    if (!isCurrent && g_requestChartTransmutation) {
                        g_requestChartTransmutation(self.unique_name, type);
                    }
                }
            }
            ImGui::EndMenu();
        }
    }

    template<typename Self>
    void drawDuplicateChartMenuItem(this Self const& self) {
        if (menu_icons::menuItemWithIcon(menu_icons::kDuplicate, "Duplicate")) {
            if (g_requestChartDuplication) {
                g_requestChartDuplication(self.unique_name);
            }
        }
    }

    template<typename Self>
    void drawRemoveChartMenuItem(this Self const& self) {
        if (menu_icons::menuItemWithIcon(menu_icons::kRemove, "Remove")) {
            if (g_requestChartRemoval) {
                g_requestChartRemoval(self.unique_name);
            }
        }
    }

    /// draws axis submenu (scale selector, auto-fit toggle, min/max editors)
    template<typename Self>
    void drawAxisSubmenu(this Self& self, AxisKind axis) {
        const bool  isX   = (axis == AxisKind::X);
        const char* label = isX ? "x-Axis" : "y-Axis";
        const char* icon  = isX ? menu_icons::kXAxis : menu_icons::kYAxis;

        if (menu_icons::beginMenuWithIcon(icon, label)) {
            self.drawAxisSubmenuContent(axis);
            ImGui::EndMenu();
        }
    }

    /// Axis submenu content with scale, auto-fit, and min/max controls.
    template<typename Self>
    void drawAxisSubmenuContent(this Self& self, AxisKind axis, std::size_t axisIndex = 0) {
        const bool      isX        = (axis == AxisKind::X);
        constexpr float kDragWidth = 70.0f;

        // Get current plot limits for the specific axis being edited
        const ImAxis targetXAxis = ImAxis_X1 + static_cast<int>(isX ? axisIndex : 0);
        const ImAxis targetYAxis = ImAxis_Y1 + static_cast<int>(isX ? 0 : axisIndex);
        const auto   plotLimits  = ImPlot::GetPlotLimits(targetXAxis, targetYAxis);
        const auto&  axisLimits  = isX ? plotLimits.X : plotLimits.Y;
        const double plotMin     = axisLimits.Min;
        const double plotMax     = axisLimits.Max;

        auto drawEnumCombo = [&]<typename E>(const char* icon, const char* text, const char* comboId, E current, auto&& setter) {
            menu_icons::iconText(icon, text);
            ImGui::SameLine();
            static const float w = [] {
                float maxW = 0.0f;
                for (auto e : magic_enum::enum_values<E>()) {
                    maxW = std::max(maxW, ImGui::CalcTextSize(magic_enum::enum_name(e).data()).x);
                }
                return maxW + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight();
            }();
            ImGui::SetNextItemWidth(w);
            if (ImGui::BeginCombo(comboId, magic_enum::enum_name(current).data())) {
                for (auto e : magic_enum::enum_values<E>()) {
                    if (ImGui::Selectable(magic_enum::enum_name(e).data(), e == current)) {
                        setter(e);
                    }
                    if (e == current) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            detail::onScrollWheel([&](float wheel) {
                auto values = magic_enum::enum_values<E>();
                auto it     = std::ranges::find(values, current);
                if (it == values.end()) {
                    return;
                }
                if (wheel > 0.0f && std::next(it) != values.end()) {
                    setter(*std::next(it));
                } else if (wheel < 0.0f && it != values.begin()) {
                    setter(*std::prev(it));
                }
            });
        };

        if constexpr (requires {
                          self.getAxisScale(AxisKind::X);
                          self.setAxisScale(AxisKind::X, AxisScale::Linear);
                      }) {
            drawEnumCombo(menu_icons::kScale, "scale:", "##scale", self.getAxisScale(axis).value_or(AxisScale::Linear), [&](AxisScale s) { self.setAxisScale(axis, s); });
        }
        if constexpr (requires {
                          self.getAxisFormat(AxisKind::X);
                          self.setAxisFormat(AxisKind::X, LabelFormat::Auto);
                      }) {
            drawEnumCombo(menu_icons::kFormat, "format:", "##format", self.getAxisFormat(axis), [&](LabelFormat f) { self.setAxisFormat(axis, f); });
        }

        if constexpr (requires {
                          self.x_min;
                          self.x_max;
                          self.y_min;
                          self.y_max;
                          self.x_auto_scale;
                          self.y_auto_scale;
                      }) {
            auto drawAutoFitControls = [&](auto& autoScaleField, auto& minField, auto& maxField) {
                bool       autoFit    = readAutoScale(autoScaleField, axisIndex);
                const bool wasAutoFit = autoFit;

                {
                    DigitizerUi::IMW::Font iconFont(DigitizerUi::LookAndFeel::instance().fontIconsSolid);
                    ImGui::TextUnformatted(menu_icons::kAutoFit);
                }
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                if (ImGui::Checkbox("##auto", &autoFit)) {
                    writeAutoScale(autoScaleField, axisIndex, autoFit);
                    if (wasAutoFit && !autoFit) {
                        writeLimit(minField, axisIndex, plotMin);
                        writeLimit(maxField, axisIndex, plotMax);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Auto-fit axis range");
                }
                ImGui::SameLine();
                if (ImGui::Button("Fit once")) {
                    writeAutoScale(autoScaleField, axisIndex, true);
                    (isX ? self._fitOnceX : self._fitOnceY)[axisIndex] = 2;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Fit axis to data once, then return to manual mode");
                }

                ImGui::SameLine();
                if (autoFit) {
                    ImGui::BeginDisabled();
                }

                double minVal = autoFit ? plotMin : readLimit(minField, axisIndex);
                double maxVal = autoFit ? plotMax : readLimit(maxField, axisIndex);

                const double range     = std::abs(maxVal - minVal);
                const double increment = (range > 0.0 && range < 1e10) ? range * 0.01 : 0.1;
                const float  dragSpeed = static_cast<float>(increment * 0.1);

                auto drawSpinner = [&](const char* id, double& val, auto&& assign) {
                    const std::string decId  = std::format("\uf146##{}_dec", id);
                    const std::string incId  = std::format("\uf0fe##{}_inc", id);
                    const std::string dragId = std::format("##{}", id);
                    {
                        DigitizerUi::IMW::Font iconFont(DigitizerUi::LookAndFeel::instance().fontIconsSolid);
                        if (ImGui::Button(decId.c_str())) {
                            val -= increment;
                            assign(val);
                        }
                    }
                    ImGui::SameLine(0.0f, 2.0f);
                    ImGui::SetNextItemWidth(kDragWidth);
                    if (ImGui::DragScalar(dragId.c_str(), ImGuiDataType_Double, &val, dragSpeed, nullptr, nullptr, "%.4g")) {
                        assign(val);
                    }
                    detail::onScrollWheel([&](float wheel) {
                        val += static_cast<double>(wheel) * increment;
                        assign(val);
                    });
                    ImGui::SameLine(0.0f, 2.0f);
                    {
                        DigitizerUi::IMW::Font iconFont(DigitizerUi::LookAndFeel::instance().fontIconsSolid);
                        if (ImGui::Button(incId.c_str())) {
                            val += increment;
                            assign(val);
                        }
                    }
                };

                auto setMin = [&](double v) { writeLimit(minField, axisIndex, v); };
                auto setMax = [&](double v) { writeLimit(maxField, axisIndex, v); };

                drawSpinner("min", minVal, setMin);
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                menu_icons::iconText(menu_icons::kArrow, "");
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                drawSpinner("max", maxVal, setMax);

                if (autoFit) {
                    ImGui::EndDisabled();
                }
            };

            if (isX) {
                drawAutoFitControls(self.x_auto_scale, self.x_min, self.x_max);
            } else {
                drawAutoFitControls(self.y_auto_scale, self.y_min, self.y_max);
            }
        }
    }

    /// draws the full history depth control widget: value input + unit combo + log slider + status line
    /// n_history (gr::Size_t, samples) is the persistent property; display unit is UI-only state
    template<typename Self>
    void drawHistoryDepthWidget(this Self& self) {
        constexpr float      kDragWidth  = 60.0f;
        constexpr float      kComboWidth = 65.0f;
        constexpr gr::Size_t kMinSamples = 4U;
        constexpr gr::Size_t kMaxSamples = 1'000'000U;

        // canonical 1-2-5 decade stops for time and sample count
        constexpr std::array kTimeStops     = {0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 10.0, 20.0, 30.0, 60.0, 120.0, 300.0, 600.0, 1800.0, 3600.0};
        constexpr float      kTimeSliderMin = 0.0f;
        constexpr float      kTimeSliderMax = static_cast<float>(kTimeStops.size() - 1);

        constexpr std::array<gr::Size_t, 22> kSampleStops     = {4U, 10U, 20U, 50U, 100U, 200U, 500U, 1'000U, 2'000U, 5'000U, 10'000U, 20'000U, 50'000U, 100'000U, 200'000U, 500'000U, 1'000'000U, 2'000'000U, 5'000'000U, 10'000'000U, 50'000'000U, 100'000'000U};
        constexpr float                      kSampleSliderMin = 0.0f;
        constexpr float                      kSampleSliderMax = static_cast<float>(kSampleStops.size() - 1);

        gr::Size_t nSamples = self.n_history.value;

        // get sample rate from first available sink
        float sampleRate = 0.0f;
        for (const auto& sink : self._signalSinks) {
            if (sink) {
                sampleRate = sink->sampleRate();
                break;
            }
        }
        const bool rateKnown = sampleRate > 0.0f;

        // compute display value in the user's chosen unit
        double displayValue = static_cast<double>(nSamples);
        if (self._historyDisplayUnit == HistoryUnit::seconds && rateKnown) {
            displayValue = static_cast<double>(nSamples) / static_cast<double>(sampleRate);
        }

        // helper: convert display value back to samples
        auto toSamples = [&](double val) -> gr::Size_t {
            if (self._historyDisplayUnit == HistoryUnit::seconds && rateKnown) {
                double samples = std::ceil(std::max(val, 0.0) * static_cast<double>(sampleRate));
                return static_cast<gr::Size_t>(std::clamp(samples, static_cast<double>(kMinSamples), static_cast<double>(kMaxSamples)));
            }
            return static_cast<gr::Size_t>(std::clamp(std::max(val, 0.0), static_cast<double>(kMinSamples), static_cast<double>(kMaxSamples)));
        };

        // helper: schedule a debounced resize
        auto scheduleResize = [&]() { self._pendingResizeTime = ImGui::GetTime() + kCapacityDebounceSeconds; };

        // --- Row 1: Icon + [- value +] [unit combo] ---
        {
            DigitizerUi::IMW::Font iconFont(DigitizerUi::LookAndFeel::instance().fontIconsSolid);
            ImGui::TextUnformatted(menu_icons::kHistory);
        }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

        // decrease button
        {
            DigitizerUi::IMW::Font iconFont(DigitizerUi::LookAndFeel::instance().fontIconsSolid);
            if (ImGui::Button("\uf146##depthDec")) { // minus-square
                double step    = std::max(self._historyDisplayUnit == HistoryUnit::seconds ? 0.1 : 100.0, displayValue * 0.1);
                displayValue   = std::max(displayValue - step, self._historyDisplayUnit == HistoryUnit::seconds ? 0.001 : static_cast<double>(kMinSamples));
                self.n_history = toSamples(displayValue);
                scheduleResize();
            }
        }
        ImGui::SameLine(0.0f, 2.0f);

        // value drag input with logarithmic speed
        ImGui::SetNextItemWidth(kDragWidth);
        float       dragSpeed = static_cast<float>(std::max(displayValue * 0.01, 0.001));
        const char* format    = (self._historyDisplayUnit == HistoryUnit::seconds) ? "%.3g" : "%.0f";
        if (ImGui::DragScalar("##historyValue", ImGuiDataType_Double, &displayValue, dragSpeed, nullptr, nullptr, format)) {
            self.n_history = toSamples(displayValue);
            scheduleResize();
        }
        detail::onScrollWheel([&](float wheel) {
            double step = std::max(self._historyDisplayUnit == HistoryUnit::seconds ? 0.1 : 100.0, displayValue * 0.1);
            displayValue += static_cast<double>(wheel) * step;
            displayValue   = std::max(displayValue, self._historyDisplayUnit == HistoryUnit::seconds ? 0.001 : static_cast<double>(kMinSamples));
            self.n_history = toSamples(displayValue);
            scheduleResize();
        });

        ImGui::SameLine(0.0f, 2.0f);
        // increase button
        {
            DigitizerUi::IMW::Font iconFont(DigitizerUi::LookAndFeel::instance().fontIconsSolid);
            if (ImGui::Button("\uf0fe##depthInc")) { // plus-square
                double step    = std::max(self._historyDisplayUnit == HistoryUnit::seconds ? 0.1 : 100.0, displayValue * 0.1);
                displayValue   = displayValue + step;
                self.n_history = toSamples(displayValue);
                scheduleResize();
            }
        }

        ImGui::SameLine();

        // unit combo (display unit only, not persisted)
        ImGui::SetNextItemWidth(kComboWidth);
        auto currentUnitName = magic_enum::enum_name(self._historyDisplayUnit);
        if (ImGui::BeginCombo("##historyUnit", currentUnitName.data())) {
            for (auto unit : magic_enum::enum_values<HistoryUnit>()) {
                bool enabled  = (unit != HistoryUnit::seconds) || rateKnown;
                bool selected = (unit == self._historyDisplayUnit);

                if (!enabled) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Selectable(magic_enum::enum_name(unit).data(), selected)) {
                    self._historyDisplayUnit = unit; // just change display, n_history stays unchanged
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                if (!enabled) {
                    ImGui::EndDisabled();
                }
            }
            ImGui::EndCombo();
        }
        detail::onScrollWheel([&](float wheel) {
            auto values = magic_enum::enum_values<HistoryUnit>();
            auto it     = std::ranges::find(values, self._historyDisplayUnit);
            if (it != values.end()) {
                if (wheel > 0.0f && std::next(it) != values.end()) {
                    self._historyDisplayUnit = *std::next(it);
                } else if (wheel < 0.0f && it != values.begin()) {
                    self._historyDisplayUnit = *std::prev(it);
                }
            }
        });

        // --- Row 2: Quick-adjust slider ---
        nSamples = self.n_history.value; // re-read after potential changes above
        ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x, 60.0f));

        if (self._historyDisplayUnit == HistoryUnit::seconds && rateKnown) {
            // seconds mode: snap to canonical time stops
            double currentSeconds = static_cast<double>(nSamples) / static_cast<double>(sampleRate);
            // find nearest stop index
            float  bestIdx  = 0.0f;
            double bestDist = std::abs(std::log(currentSeconds) - std::log(kTimeStops[0]));
            for (std::size_t i = 1; i < kTimeStops.size(); ++i) {
                double dist = std::abs(std::log(currentSeconds) - std::log(kTimeStops[i]));
                if (dist < bestDist) {
                    bestDist = dist;
                    bestIdx  = static_cast<float>(i);
                }
            }

            auto formatTimeStop = [](double seconds) -> std::string {
                if (seconds >= 60.0) {
                    return std::format("{:.0f} min", seconds / 60.0);
                }
                if (seconds >= 1.0) {
                    return std::format("{:.0f} s", seconds);
                }
                if (seconds >= 0.01) {
                    return std::format("{:.0f} ms", seconds * 1e3);
                }
                return std::format("{:.1f} ms", seconds * 1e3);
            };

            if (ImGui::SliderFloat("##historySlider", &bestIdx, kTimeSliderMin, kTimeSliderMax, formatTimeStop(kTimeStops[static_cast<std::size_t>(std::round(bestIdx))]).c_str(), ImGuiSliderFlags_NoInput)) {
                std::size_t idx        = static_cast<std::size_t>(std::clamp(std::round(bestIdx), kTimeSliderMin, kTimeSliderMax));
                double      seconds    = kTimeStops[idx];
                gr::Size_t  newSamples = static_cast<gr::Size_t>(std::ceil(seconds * static_cast<double>(sampleRate)));
                self.n_history         = std::clamp(newSamples, kMinSamples, kMaxSamples);
                scheduleResize();
            }
            detail::onScrollWheel([&](float wheel) {
                bestIdx               = std::clamp(std::round(bestIdx) + ((wheel > 0.0f) ? 1.0f : -1.0f), kTimeSliderMin, kTimeSliderMax);
                auto       idx        = static_cast<std::size_t>(bestIdx);
                double     seconds    = kTimeStops[idx];
                gr::Size_t newSamples = static_cast<gr::Size_t>(std::ceil(seconds * static_cast<double>(sampleRate)));
                self.n_history        = std::clamp(newSamples, kMinSamples, kMaxSamples);
                scheduleResize();
            });
        } else {
            // samples mode: 1-2-5 decade stop sequence
            float  bestIdx  = 0.0f;
            double bestDist = std::abs(std::log(static_cast<double>(nSamples)) - std::log(static_cast<double>(kSampleStops[0])));
            for (std::size_t i = 1; i < kSampleStops.size(); ++i) {
                double dist = std::abs(std::log(static_cast<double>(nSamples)) - std::log(static_cast<double>(kSampleStops[i])));
                if (dist < bestDist) {
                    bestDist = dist;
                    bestIdx  = static_cast<float>(i);
                }
            }

            auto formatSampleStop = [](gr::Size_t samples) -> std::string {
                if (samples >= 1'000'000U) {
                    return std::format("{:.0f}M", static_cast<double>(samples) / 1e6);
                }
                if (samples >= 1'000U) {
                    return std::format("{:.0f}k", static_cast<double>(samples) / 1e3);
                }
                return std::format("{}", samples);
            };

            if (ImGui::SliderFloat("##historySlider", &bestIdx, kSampleSliderMin, kSampleSliderMax, formatSampleStop(kSampleStops[static_cast<std::size_t>(std::round(bestIdx))]).c_str(), ImGuiSliderFlags_NoInput)) {
                auto idx       = static_cast<std::size_t>(std::clamp(std::round(bestIdx), kSampleSliderMin, kSampleSliderMax));
                self.n_history = std::clamp(kSampleStops[idx], kMinSamples, kMaxSamples);
                scheduleResize();
            }
            detail::onScrollWheel([&](float wheel) {
                bestIdx        = std::clamp(std::round(bestIdx) + ((wheel > 0.0f) ? 1.0f : -1.0f), kSampleSliderMin, kSampleSliderMax);
                auto idx       = static_cast<std::size_t>(bestIdx);
                self.n_history = std::clamp(kSampleStops[idx], kMinSamples, kMaxSamples);
                scheduleResize();
            });
        }

        // --- Row 3: Status line ---
        nSamples = self.n_history.value;
        if (self._historyDisplayUnit == HistoryUnit::seconds) {
            if (rateKnown) {
                ImGui::TextDisabled("= %zu samples @ %.0f Hz", static_cast<std::size_t>(nSamples), static_cast<double>(sampleRate));
            } else {
                ImGui::TextDisabled("sample rate unknown");
            }
        } else {
            if (rateKnown) {
                double      durationSeconds = static_cast<double>(nSamples) / static_cast<double>(sampleRate);
                std::string durationStr;
                if (durationSeconds >= 1.0) {
                    durationStr = std::format("{:.2f} s", durationSeconds);
                } else if (durationSeconds >= 0.001) {
                    durationStr = std::format("{:.2f} ms", durationSeconds * 1000.0);
                } else {
                    durationStr = std::format("{:.2f} \xc2\xb5s", durationSeconds * 1'000'000.0); // µs
                }
                ImGui::TextDisabled("\xe2\x89\x88 %s @ %.0f Hz", durationStr.c_str(), static_cast<double>(sampleRate)); // ≈
            }
        }
    }

    template<typename Self>
    void updateAllSinksCapacity(this Self& self) {
        std::size_t capacity = kDefaultHistorySize;
        if constexpr (requires { self.n_history.value; }) {
            capacity = static_cast<std::size_t>(self.n_history.value);
        }
        for (auto& sink : self._signalSinks) {
            if (sink) {
                sink->requestCapacity(std::string(self.unique_name), capacity);
            }
        }
        self._lastCapacityRefreshTime = ImGui::GetTime();
    }

    /// processes pending debounced resize and periodic capacity refresh
    template<typename Self>
    void refreshCapacityIfNeeded(this Self& self) {
        const double now = ImGui::GetTime();

        // debounced resize: apply pending capacity change after user stops adjusting
        if (self._pendingResizeTime > 0.0 && now >= self._pendingResizeTime) {
            self._pendingResizeTime = 0.0;
            self.updateAllSinksCapacity();
        }

        // periodic refresh to keep capacity requests alive (before 60s expiry)
        if ((now - self._lastCapacityRefreshTime) >= kCapacityRefreshIntervalSeconds) {
            self.updateAllSinksCapacity();
        }
    }

    template<typename Self>
    DrawPrologue prepareDrawPrologue(this Self& self, const gr::property_map& config) {
        self.processAcceptedDndRemoval();
        self.syncSinksIfNeeded(self.data_sinks.value);
        self.refreshCapacityIfNeeded();

        bool layoutMode = false;
        if (const auto it = config.find("layoutMode"); it != config.end()) {
            layoutMode = it->second.value_or(false);
        }

        bool effectiveShowLegend = false;
        if constexpr (requires { self.show_legend; }) {
            effectiveShowLegend = self.show_legend.value || layoutMode;
        }

        float       layoutOffset = layoutMode ? 5.f : 0.f;
        ImVec2      plotSize     = ImGui::GetContentRegionAvail() - ImVec2(2 * layoutOffset, 2 * layoutOffset);
        ImPlotFlags plotFlags    = ImPlotFlags_NoChild | ImPlotFlags_NoMouseText | ImPlotFlags_NoTitle | ImPlotFlags_NoMenus;
        if (!effectiveShowLegend) {
            plotFlags |= ImPlotFlags_NoLegend;
        }

        bool effectiveShowGrid = true;
        if constexpr (requires { self.show_grid; }) {
            effectiveShowGrid = self.show_grid.value;
        }

        return DrawPrologue{plotFlags, plotSize, effectiveShowLegend, layoutMode, effectiveShowGrid};
    }

    template<typename Self>
    void handleSettingsChanged(this Self& self, const gr::property_map& newSettings) {
        if (newSettings.contains("data_sinks")) {
            self.onDataSinksChanged(self.data_sinks.value);
        }
        if constexpr (requires { self.n_history; }) {
            if (newSettings.contains("n_history")) {
                self.updateAllSinksCapacity();
            }
        }
    }

    template<typename Self>
    [[nodiscard]] std::optional<AxisScale> getAxisScale(this const Self& self, AxisKind axis) noexcept {
        if (auto cfg = parseAxisConfig(self.ui_constraints.value, axis)) {
            return cfg->scale;
        }
        return std::nullopt;
    }

    template<typename Self>
    [[nodiscard]] LabelFormat getAxisFormat(this const Self& self, AxisKind axis) noexcept {
        if (auto cfg = parseAxisConfig(self.ui_constraints.value, axis)) {
            return cfg->format;
        }
        return LabelFormat::Auto;
    }

    template<typename Self, typename E>
    void setAxisConstraintField(this Self& self, AxisKind axis, std::string_view key, E value) {
        static constexpr std::array kAxisNames  = {"X", "Y", "Z"};
        auto&                       constraints = self.ui_constraints.value;

        gr::Tensor<gr::pmt::Value> axesVec;
        if (const auto it = constraints.find("axes"); it != constraints.end()) {
            if (const auto* existing = it->second.template get_if<gr::Tensor<gr::pmt::Value>>()) {
                axesVec = *existing;
            }
        }

        const std::string targetAxis = kAxisNames[static_cast<std::size_t>(axis)];
        bool              found      = false;
        for (auto& axisPmt : axesVec) {
            auto* axisMap = axisPmt.get_if<gr::property_map>();
            if (axisMap == nullptr) {
                continue;
            }
            auto axisStrIt = axisMap->find("axis");
            if (axisStrIt == axisMap->end()) {
                continue;
            }
            if (!axisStrIt->second.is_string()) {
                continue;
            }
            const auto axisStr = axisStrIt->second.value_or(std::string_view{});
            if (axisStr == targetAxis || (axisStr.size() == 1 && std::tolower(axisStr[0]) == std::tolower(targetAxis[0]))) {
                (*axisMap)[std::pmr::string(key)] = std::pmr::string(magic_enum::enum_name(value));
                found                             = true;
                break;
            }
        }
        if (!found) {
            gr::property_map newAxis;
            newAxis["axis"]                = targetAxis;
            newAxis[std::pmr::string(key)] = std::pmr::string(magic_enum::enum_name(value));
            axesVec.push_back(newAxis);
        }
        constraints["axes"] = axesVec;
        std::ignore         = self.settings().set({{"ui_constraints", constraints}});
    }

    template<typename Self>
    void setAxisScale(this Self& self, AxisKind axis, AxisScale scale) {
        self.setAxisConstraintField(axis, "scale", scale);
    }

    template<typename Self>
    void setAxisFormat(this Self& self, AxisKind axis, LabelFormat format) {
        self.setAxisConstraintField(axis, "format", format);
    }

    template<typename Self>
    void handleCommonInteractions(this Self& self) {
        if constexpr (requires {
                          self.x_auto_scale;
                          self.y_auto_scale;
                          self.x_min;
                          self.x_max;
                          self.y_min;
                          self.y_max;
                      }) {
            // fit-once state machine and zoom detection — per-axis
            auto detectUserZoom = [](bool autoScale, auto& autoScaleField, auto& minField, auto& maxField, std::size_t idx, double plotMin, double plotMax, double prevMin, double prevMax, bool forceApplied) {
                if (!autoScale || forceApplied || !std::isfinite(prevMin) || !std::isfinite(prevMax)) {
                    return;
                }
                const double range = std::abs(prevMax - prevMin);
                const double tol   = std::max(range * 1e-3, 1e-10);
                if (std::abs(plotMin - prevMin) > tol || std::abs(plotMax - prevMax) > tol) {
                    writeAutoScale(autoScaleField, idx, false);
                    writeLimit(minField, idx, plotMin);
                    writeLimit(maxField, idx, plotMax);
                }
            };

            for (std::size_t i = 0; i < 3; ++i) {
                auto limits = ImPlot::GetPlotLimits(ImAxis_X1 + static_cast<int>(i), ImAxis_Y1 + static_cast<int>(i));

                // fit-once: auto-fit needs one full frame to resolve, then capture limits
                if (self._fitOnceX[i] == 1) {
                    self._fitOnceX[i] = 0;
                    writeAutoScale(self.x_auto_scale, i, false);
                    writeLimit(self.x_min, i, limits.X.Min);
                    writeLimit(self.x_max, i, limits.X.Max);
                } else if (self._fitOnceX[i] > 1) {
                    --self._fitOnceX[i];
                }
                if (self._fitOnceY[i] == 1) {
                    self._fitOnceY[i] = 0;
                    writeAutoScale(self.y_auto_scale, i, false);
                    writeLimit(self.y_min, i, limits.Y.Min);
                    writeLimit(self.y_max, i, limits.Y.Max);
                } else if (self._fitOnceY[i] > 1) {
                    --self._fitOnceY[i];
                }

                detectUserZoom(readAutoScale(self.x_auto_scale, i), self.x_auto_scale, self.x_min, self.x_max, i, limits.X.Min, limits.X.Max, self._prevXMin[i], self._prevXMax[i], self._limitsForceAppliedX[i]);
                detectUserZoom(readAutoScale(self.y_auto_scale, i), self.y_auto_scale, self.y_min, self.y_max, i, limits.Y.Min, limits.Y.Max, self._prevYMin[i], self._prevYMax[i], self._limitsForceAppliedY[i]);
            }
        }

        std::string popupId = std::string(std::remove_cvref_t<Self>::kChartTypeName) + "ContextMenu";
        self.drawContextMenu(popupId.c_str());
        self.setupLegendDragSources();
        self.handlePlotDropTarget();
    }

    template<typename Self>
    void drawEmptyPlot(this Self& self, const char* message, ImPlotFlags plotFlags, const ImVec2& size) {
        if (DigitizerUi::TouchHandler<>::BeginZoomablePlot(self.chart_name.value, size, plotFlags)) {
            ImPlot::SetupAxis(ImAxis_X1, "X", ImPlotAxisFlags_None);
            ImPlot::SetupAxis(ImAxis_Y1, "Y", ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_X1, -1.0, 1.0, ImPlotCond_Once);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -1.0, 1.0, ImPlotCond_Once);
            ImPlot::SetupFinish();
            auto limits = ImPlot::GetPlotLimits();
            ImPlot::PlotText(message, (limits.X.Min + limits.X.Max) / 2, (limits.Y.Min + limits.Y.Max) / 2);
            tooltip::showPlotMouseTooltip();
            self.handleCommonInteractions();
            DigitizerUi::TouchHandler<>::EndZoomablePlot();
        }
    }

    template<typename FieldType>
    static void drawAutoSettingWidget(FieldType& field, std::string_view fieldName) {
        using ValueType              = FieldType::value_type;
        using LimitType              = FieldType::LimitType;
        constexpr float kWidgetWidth = 120.0f;
        constexpr bool  hasLimits    = (LimitType::MinRange != LimitType::MaxRange);
        constexpr bool  useLog       = hasLimits && (static_cast<double>(LimitType::MaxRange) - static_cast<double>(LimitType::MinRange)) > 100'000.0;

        const auto             label       = std::format("##{}", fieldName);
        const ImGuiSliderFlags sliderFlags = useLog ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None;

        if constexpr (std::is_same_v<ValueType, bool>) {
            bool val = field.value;
            if (ImGui::Checkbox(label.c_str(), &val)) {
                field = val;
            }
        } else if constexpr (std::is_floating_point_v<ValueType>) {
            ImGui::SetNextItemWidth(kWidgetWidth);
            float val = static_cast<float>(field.value);
            if constexpr (hasLimits) {
                constexpr float lo = static_cast<float>(LimitType::MinRange);
                constexpr float hi = static_cast<float>(LimitType::MaxRange);
                if (ImGui::SliderFloat(label.c_str(), &val, lo, hi, "%.2f", sliderFlags)) {
                    field = static_cast<ValueType>(val);
                }
                detail::onScrollWheel([&](float wheel) {
                    if constexpr (useLog) {
                        val *= std::pow(1.1f, wheel);
                    } else {
                        val += wheel * (hi - lo) * 0.01f;
                    }
                    field = static_cast<ValueType>(std::clamp(val, lo, hi));
                });
            } else {
                if (ImGui::DragFloat(label.c_str(), &val, 0.01f, 0.0f, 0.0f, "%.2f")) {
                    field = static_cast<ValueType>(val);
                }
                detail::onScrollWheel([&](float wheel) {
                    val += wheel * std::max(0.01f, std::abs(val) * 0.05f);
                    field = static_cast<ValueType>(val);
                });
            }
        } else if constexpr (std::is_integral_v<ValueType> && !std::is_same_v<ValueType, bool>) {
            bool isColourPicker = false;
            if constexpr (std::is_same_v<ValueType, std::uint32_t>) {
                if (FieldType::description().find("colo") != std::string_view::npos) {
                    isColourPicker = true;
                    ImVec4 col     = sinkColor(field.value);
                    float  rgb[3]{col.x, col.y, col.z};
                    if (ImGui::ColorEdit3(label.c_str(), rgb, ImGuiColorEditFlags_NoInputs)) {
                        field = (static_cast<std::uint32_t>(rgb[0] * 255.0f) << 16) | (static_cast<std::uint32_t>(rgb[1] * 255.0f) << 8) | static_cast<std::uint32_t>(rgb[2] * 255.0f);
                    }
                }
            }
            if (!isColourPicker) {
                ImGui::SetNextItemWidth(kWidgetWidth);
                int val = static_cast<int>(field.value);
                if constexpr (hasLimits) {
                    constexpr int lo = static_cast<int>(LimitType::MinRange);
                    constexpr int hi = static_cast<int>(LimitType::MaxRange);
                    if (ImGui::SliderInt(label.c_str(), &val, lo, hi, "%d", sliderFlags)) {
                        field = static_cast<ValueType>(val);
                    }
                    detail::onScrollWheel([&](float wheel) {
                        if constexpr (useLog) {
                            int newVal = static_cast<int>(std::round(static_cast<float>(val) * std::pow(1.1f, wheel)));
                            if (newVal == val) {
                                newVal += (wheel > 0.0f) ? 1 : -1;
                            }
                            val = std::clamp(newVal, lo, hi);
                        } else {
                            val = std::clamp(val + static_cast<int>(wheel) * std::max(1, (hi - lo) / 100), lo, hi);
                        }
                        field = static_cast<ValueType>(val);
                    });
                } else {
                    if (ImGui::DragInt(label.c_str(), &val, 1.0f)) {
                        field = static_cast<ValueType>(val);
                    }
                    detail::onScrollWheel([&](float wheel) {
                        val += static_cast<int>(wheel) * std::max(1, std::abs(val) / 20);
                        field = static_cast<ValueType>(val);
                    });
                }
            }
        } else if constexpr (std::is_same_v<ValueType, ImPlotColormap_>) {
            ImGui::SetNextItemWidth(kWidgetWidth);
            auto current = static_cast<ImPlotColormap>(field.value);
            if (ImGui::BeginCombo(label.c_str(), ImPlot::GetColormapName(current))) {
                const float gradientWidth = ImGui::GetContentRegionAvail().x;
                const float lineHeight    = ImGui::GetTextLineHeight();
                for (int i = 0; i < ImPlot::GetColormapCount(); ++i) {
                    bool   selected = (i == current);
                    ImVec2 pos      = ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable(std::format("##{}", i).c_str(), selected, 0, ImVec2(gradientWidth, lineHeight))) {
                        field = static_cast<ImPlotColormap_>(i);
                    }
                    auto*       drawList = ImGui::GetWindowDrawList();
                    const float step     = gradientWidth / 32.0f;
                    for (int s = 0; s < 32; ++s) {
                        float  t0 = static_cast<float>(s) / 32.0f;
                        float  t1 = static_cast<float>(s + 1) / 32.0f;
                        ImVec4 c0 = ImPlot::SampleColormap(t0, i);
                        ImVec4 c1 = ImPlot::SampleColormap(t1, i);
                        drawList->AddRectFilledMultiColor(ImVec2(pos.x + step * static_cast<float>(s), pos.y), ImVec2(pos.x + step * static_cast<float>(s + 1), pos.y + lineHeight), ImGui::GetColorU32(c0), ImGui::GetColorU32(c1), ImGui::GetColorU32(c1), ImGui::GetColorU32(c0));
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", ImPlot::GetColormapName(i));
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        } else if constexpr (std::is_enum_v<ValueType>) {
            ImGui::SetNextItemWidth(kWidgetWidth);
            auto current = field.value;
            if (ImGui::BeginCombo(label.c_str(), magic_enum::enum_name(current).data())) {
                for (auto e : magic_enum::enum_values<ValueType>()) {
                    bool selected = (e == current);
                    if (ImGui::Selectable(magic_enum::enum_name(e).data(), selected)) {
                        field = e;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        } else if constexpr (std::is_same_v<ValueType, std::string>) {
            ImGui::SetNextItemWidth(kWidgetWidth);
            std::array<char, 256> buf{};
            std::ranges::copy(field.value | std::views::take(buf.size() - 1), buf.begin());
            if (ImGui::InputText(label.c_str(), buf.data(), buf.size())) {
                field = std::string(buf.data());
            }
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(FieldType::description().data());
    }

    template<typename Self>
    void drawAutoGeneratedSettings(this Self& self, bool visibleOnly) {
        using SelfType = std::remove_cvref_t<Self>;
        gr::refl::for_each_data_member_index<SelfType>([&](auto kIdx) {
            constexpr std::size_t idx = decltype(kIdx)::value;
            using MemberType          = gr::refl::data_member_type<SelfType, idx>;
            using RawType             = std::remove_cvref_t<MemberType>;

            if constexpr (gr::AnnotatedType<RawType>) {
                constexpr std::string_view name = gr::refl::data_member_name<SelfType, idx>;
                if constexpr (!detail::isExcludedFromAutoSettings(name)) {
                    constexpr bool isVisible = RawType::visible();
                    if (isVisible == visibleOnly) {
                        auto& field = gr::refl::data_member<idx>(self);
                        drawAutoSettingWidget(field, name);
                    }
                }
            }
        });
    }

    template<typename Self>
    void drawSettingsSubmenu(this Self& self) {
        if (menu_icons::beginMenuWithIcon(menu_icons::kSettings, "Settings")) {
            self.drawAutoGeneratedSettings(/*visibleOnly=*/false);
            if constexpr (requires { self.customMenuCallback; }) {
                if (self.customMenuCallback) {
                    ImGui::Separator();
                    self.customMenuCallback();
                }
            }
            ImGui::EndMenu();
        }
    }

    template<typename Self>
    void drawCommonContextMenuItems(this Self& self) {
        if constexpr (requires { self.n_history; }) {
            self.drawHistoryDepthWidget();
        }
        ImGui::Separator();

        self.drawSettingsSubmenu();
        self.drawAutoGeneratedSettings(/*visibleOnly=*/true);

        ImGui::Separator();
        self.drawChartTypeSubmenu();
        self.drawDuplicateChartMenuItem();
        self.drawRemoveChartMenuItem();
    }

    /// draws axis-specific or full context menu depending on what was right-clicked
    template<typename Self>
    void drawContextMenu(this Self& self, const char* popupId = "ChartContextMenu") {
        // detect which specific axis (if any) is hovered
        std::optional<ImAxis> hoveredAxis;
        for (int i = 0; i < 3; ++i) {
            if (ImPlot::IsAxisHovered(ImAxis_X1 + i)) {
                hoveredAxis = ImAxis_X1 + i;
                break;
            }
            if (ImPlot::IsAxisHovered(ImAxis_Y1 + i)) {
                hoveredAxis = ImAxis_Y1 + i;
                break;
            }
        }

        const bool rightClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Right);

        if (hoveredAxis && rightClicked) {
            self._hoveredAxisForMenu = hoveredAxis;
            ImGui::OpenPopup("AxisContextMenu");
        } else if (ImPlot::IsPlotHovered() && !hoveredAxis && rightClicked) {
            ImGui::OpenPopup(popupId);
        }

        // detect double-click on any axis for fit-once
        if constexpr (requires {
                          self.x_auto_scale;
                          self.y_auto_scale;
                      }) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                for (int i = 0; i < 3; ++i) {
                    auto idx = static_cast<std::size_t>(i);
                    if (ImPlot::IsAxisHovered(ImAxis_X1 + i) && self._fitOnceX[idx] == 0) {
                        writeAutoScale(self.x_auto_scale, idx, true);
                        self._fitOnceX[idx] = 2;
                    }
                    if (ImPlot::IsAxisHovered(ImAxis_Y1 + i) && self._fitOnceY[idx] == 0) {
                        writeAutoScale(self.y_auto_scale, idx, true);
                        self._fitOnceY[idx] = 2;
                    }
                }
            }
        }

        // axis-specific popup: show only controls for the clicked axis
        if (ImGui::BeginPopup("AxisContextMenu")) {
            if (self._hoveredAxisForMenu) {
                const ImAxis      axis      = *self._hoveredAxisForMenu;
                const bool        isX       = (axis == ImAxis_X1 || axis == ImAxis_X2 || axis == ImAxis_X3);
                const std::size_t axisIndex = static_cast<std::size_t>(isX ? (axis - ImAxis_X1) : (axis - ImAxis_Y1));
                self.drawAxisSubmenuContent(isX ? AxisKind::X : AxisKind::Y, axisIndex);
            }
            ImGui::EndPopup();
        }

        // full canvas popup
        if (ImGui::BeginPopup(popupId)) {
            self.drawCommonContextMenuItems();
            ImGui::EndPopup();
        }
    }
};

} // namespace opendigitizer::charts

#endif // OPENDIGITIZER_CHARTS_CHART_HPP
