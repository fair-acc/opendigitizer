#include "DashboardPreview.hpp"

#include "../GraphModel.hpp"
#include "../MapUtils.hpp"
#include "../common/LookAndFeel.hpp"
#include "../utils/ImGuiDockSpaceState.hpp"
#include "../utils/TransparentStringHash.hpp"
#include "Docking.hpp"
#include "SignalColours.hpp"

#include <implot.h>
#include <implot_internal.h>

#include <gnuradio-4.0/PmtTypeHelpers.hpp>
#include <gnuradio-4.0/YamlPmt.hpp>

#include <magic_enum.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace gr::od {

namespace {

template<typename T>
T getAndConvertNumber(const gr::property_map& map, std::string_view key, T fallback) {
    return getOptionalProperty<T, true>(map, key).value_or(fallback);
}

auto tryGetTensor(const auto& map, std::string_view key) { return getOptionalProperty<gr::Tensor<gr::pmt::Value>>(map, key); }

template<typename T>
using StringMap = std::unordered_map<std::string, T, TransparentStringHash, std::equal_to<>>;
using StringSet = std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>;

StringSet getNamesOfAllBlocksWithAnInputConnection(const gr::property_map& flowgraphRoot) {
    StringSet  blocksWithInput;
    const auto connections = tryGetTensor(flowgraphRoot, "connections");
    if (!connections) {
        return blocksWithInput;
    }
    for (const auto& connectionValue : *connections) {
        if (const auto connection = connectionValue.get_if<gr::TensorView<gr::pmt::Value>>(); connection && connection->size() >= 3UZ && (*connection)[2].is_string()) {
            blocksWithInput.emplace((*connection)[2].value_or(std::string{}));
        }
    }
    return blocksWithInput;
}

struct PlotSinkDetails {
    DashboardPreview::Signal signal{};
    bool                     hasInput = false;
};

struct FlowgraphSinks {
    StringSet                  allBlockNames;
    StringMap<PlotSinkDetails> imPlotSinkDetailsByName;
};

FlowgraphSinks parseFlowgraphSinksFromYaml(const gr::property_map& flowgraphRoot) {
    FlowgraphSinks sinks;

    const auto blocks = tryGetTensor(flowgraphRoot, "blocks");
    if (!blocks) {
        return sinks;
    }
    const auto namesOfBlocksWithInputs = getNamesOfAllBlocksWithAnInputConnection(flowgraphRoot);

    SignalColours colours;
    for (const auto& blockValue : *blocks) { // assumes that regular dashboard loading also loads blocks in order they appear in the file
        const auto        block      = blockValue.value_or<gr::property_map>(gr::property_map{});
        const auto        parameters = block.value_or<gr::property_map>("parameters", gr::property_map{});
        const std::string name       = parameters.value_or<std::string>("name", std::string{});
        if (name.empty()) {
            continue;
        }
        sinks.allBlockNames.insert(name);

        const std::string id = block.value_or<std::string>("id", std::string{});
        if (!isPlotSinkTypeName(id)) {
            continue;
        }
        const std::uint32_t explicitColor = getAndConvertNumber<std::uint32_t>(parameters, "color", 0U);

        std::size_t slot = colours.acquire();
        colours.release(slot);
        slot = explicitColor == 0U ? colours.acquire() : colours.acquire(explicitColor);
        PlotSinkDetails details{
            .signal =
                {
                    .color     = colours.colour(slot),
                    .isDataSet = isDataSetPlotSinkTypeName(id),
                },
            .hasInput = namesOfBlocksWithInputs.contains(name),
        };
        sinks.imPlotSinkDetailsByName.emplace(name, details);
    }
    return sinks;
}

struct ChartParsingContext {
    const gr::property_map& inputYaml;
    const StringMap<int>&   inputChartIndexByName;
    DashboardPreview&       outputPreview;
};

void parseDockedChartRectAreas(const ChartParsingContext& context, DashboardPreview::Rect area) {
    const auto split = dock_space::findSplit(context.inputYaml);
    if (!split) {
        return;
    }
    const gr::property_map& inner = split->inner;
    const float             ratio = std::clamp(getAndConvertNumber(inner, dock_space::kRatio, 1.f), 0.05f, 0.95f);

    const auto placeChild = [&](std::string_view key, DashboardPreview::Rect childArea) {
        const auto child = inner.find_value(key);
        if (!child) {
            return;
        }
        auto& chartIndexByName = context.inputChartIndexByName;
        if (child->is_string()) {
            if (const auto chartIt = chartIndexByName.find(child->value_or(std::string{})); chartIt != chartIndexByName.end()) {
                context.outputPreview.charts[static_cast<std::size_t>(chartIt->second)].rect = childArea;
            }
        } else if (const auto childMap = child->get_if<gr::property_map>()) {
            parseDockedChartRectAreas(
                ChartParsingContext{
                    .inputYaml             = *childMap,
                    .inputChartIndexByName = context.inputChartIndexByName,
                    .outputPreview         = context.outputPreview,
                },
                childArea);
        }
    };

    // first is left or top, second right or bottom; the ratio is the left node's in an hsplit, the bottom node's in a vsplit
    if (split->horizontal) {
        placeChild(dock_space::kFirst, {.x = area.x, .y = area.y, .w = area.w * ratio, .h = area.h});
        placeChild(dock_space::kSecond, {.x = area.x + area.w * ratio, .y = area.y, .w = area.w * (1.f - ratio), .h = area.h});
    } else {
        placeChild(dock_space::kFirst, {.x = area.x, .y = area.y, .w = area.w, .h = area.h * (1.f - ratio)});
        placeChild(dock_space::kSecond, {.x = area.x, .y = area.y + area.h * (1.f - ratio), .w = area.w, .h = area.h * ratio});
    }
}

void parseFloatingChartRectAreas(const ChartParsingContext& context) {
    for (const auto& [windowName, windowValue] : context.inputYaml) {
        const auto chartIt = context.inputChartIndexByName.find(std::string(windowName));
        if (chartIt == context.inputChartIndexByName.end()) {
            continue;
        }
        const auto window = windowValue.get_if<gr::property_map>();
        if (!window) {
            continue;
        }
        DashboardPreview::Chart& chart = context.outputPreview.charts[static_cast<std::size_t>(chartIt->second)];
        chart.isFloating               = true;

        chart.rect = {
            .x = getAndConvertNumber(*window, dock_space::kX, 0.f),
            .y = getAndConvertNumber(*window, dock_space::kY, 0.f),
            .w = getAndConvertNumber(*window, dock_space::kWidth, 0.f),
            .h = getAndConvertNumber(*window, dock_space::kHeight, 0.f),
        };
    }
}

[[nodiscard]] bool assignChartRectAreasIfManuallySpecified(DashboardPreview& preview, DockingLayoutType layoutType, const std::vector<std::array<std::int64_t, 4>>& gridRects) {
    const std::size_t numCharts = preview.charts.size();
    if (numCharts == 0UZ || numCharts != gridRects.size() || layoutType != DockingLayoutType::Free) {
        return false;
    }
    const auto rects = freeLayoutRects(gridRects);
    for (std::size_t i = 0UZ; i < numCharts; ++i) {
        preview.charts[i].rect = rects[i];
    }
    return true;
}

void assignChartRectAreasFromAutoLayoutRules(DashboardPreview& preview, DockingLayoutType layoutType) {
    const auto rects = autoLayoutRects(layoutType, preview.charts.size());
    for (std::size_t i = 0UZ; i < rects.size(); ++i) {
        preview.charts[i].rect = rects[i];
    }
}

DashboardPreview::ChartType chartTypeFromName(std::string_view typeName) {
    using enum DashboardPreview::ChartType;
    if (typeName.contains("XYChart") || typeName.contains("YYChart")) {
        return Chart2D;
    }
    for (const auto chartType : gr::meta::enumValues<DashboardPreview::ChartType>()) {
        if (const auto name = gr::meta::enumName(chartType); name && typeName.contains(*name)) {
            return chartType;
        }
    }
    return Other;
}

ImU32 signalColor(const DashboardPreview::Signal& signal) { return signal.color == 0U ? ImGui::GetColorU32(ImGuiCol_PlotLines) : rgbToImGuiABGR(signal.color); }

[[nodiscard]] float spectrumPointHeight(float pointIndex, float t) {
    constexpr float kMinHeight             = 0.06f;
    constexpr float kMaxHeight             = 0.95f;
    constexpr float kStartingPeakMinOffset = 0.15f;
    constexpr float kStartingPeakWidth     = 0.5f;
    constexpr float kPeakSpacing           = 0.618f; // wraps around, so this is good for preventing overlap
    struct Peak {
        float offset;
        float height;
        float harshness;
    };
    constexpr std::array kPeaks = {
        Peak{.offset = 0.f, .height = 0.8f, .harshness = 250.f},
        Peak{.offset = 0.33f, .height = 0.5f, .harshness = 350.f},
        Peak{.offset = 0.57f, .height = 0.3f, .harshness = 450.f},
    };

    const float mainPeak = kStartingPeakMinOffset + kStartingPeakWidth * std::fmod(pointIndex * kPeakSpacing, 1.f);
    float       value    = kMinHeight;
    for (const Peak& peak : kPeaks) {
        const float position = std::fmod(mainPeak + peak.offset, 1.f);
        value += peak.height * std::exp(-peak.harshness * (t - position) * (t - position));
    }

    return 1.f - std::min(value, kMaxHeight); // y is down
}

[[nodiscard]] float sineWavePointHeight(float pointIndex, float t) {
    constexpr float kBasePeriod   = 1.5f;
    constexpr float kPeriodOffset = 0.5f;
    constexpr float kPhaseOffset  = 1.9f;
    constexpr float kAmplitude    = 0.32f; // big, but < 1 to stay within the chart area

    const float periods = kBasePeriod + kPeriodOffset * pointIndex;
    return 0.5f - kAmplitude * std::sin(2.f * std::numbers::pi_v<float> * periods * t + kPhaseOffset * pointIndex);
}

/// stand-in lines: a spectrum for DataSet sinks, else a sine wave
void drawSignalLines(ImDrawList& drawList, std::span<const DashboardPreview::Signal> signals, ImVec2 origin, ImVec2 size, bool forceSpectrumStyle) {
    const float   lineThickness = LookAndFeel::instance().openDashboardPageStyle.dashboardPreviewLineThickness;
    constexpr int kNumPoints    = 24;

    std::array<ImVec2, kNumPoints> points{};
    for (std::size_t signalIndex = 0UZ; signalIndex < signals.size(); ++signalIndex) {
        const float indexFloat    = static_cast<float>(signalIndex);
        const bool  spectrumStyle = forceSpectrumStyle || signals[signalIndex].isDataSet;
        for (std::size_t point = 0; point < kNumPoints; ++point) {
            const float t = static_cast<float>(point) / static_cast<float>(kNumPoints - 1);
            const float y = spectrumStyle ? spectrumPointHeight(indexFloat, t) : sineWavePointHeight(indexFloat, t);
            points[point] = {origin.x + t * size.x, origin.y + y * size.y};
        }
        drawList.AddPolyline(points.data(), kNumPoints, signalColor(signals[signalIndex]), ImDrawFlags_None, lineThickness);
    }
}

void drawSurfacePlotBoxIcon(ImDrawList& drawList, ImVec2 origin, ImVec2 size) {
    constexpr float kSideHalfHeightPercent = 0.3f;  // length of flat sides / 2
    constexpr float kPointOffsetPercent    = 0.11f; // length of pointy triangles of hexagon

    const float  unit           = std::min(size.x, size.y);
    const ImVec2 center         = {origin.x + 0.5f * size.x, origin.y + 0.5f * size.y};
    const float  halfWidth      = (kSideHalfHeightPercent + kPointOffsetPercent) * unit;
    const float  sideHalfHeight = kSideHalfHeightPercent * unit;
    const float  pointOffset    = kPointOffsetPercent * unit;

    const ImU32      color   = ImGui::GetColorU32(ImGuiCol_PlotLines);
    const std::array outline = {
        ImVec2{center.x, center.y - sideHalfHeight - pointOffset},
        ImVec2{center.x + halfWidth, center.y - sideHalfHeight},
        ImVec2{center.x + halfWidth, center.y + sideHalfHeight},
        ImVec2{center.x, center.y + sideHalfHeight + pointOffset},
        ImVec2{center.x - halfWidth, center.y + sideHalfHeight},
        ImVec2{center.x - halfWidth, center.y - sideHalfHeight},
    };
    const ImVec2 backCorner{center.x, center.y + sideHalfHeight - pointOffset};
    const ImVec2 leftBottom  = outline[4];
    const ImVec2 rightBottom = outline[2];
    const ImVec2 top         = outline[0];

    const float lineThickness = LookAndFeel::instance().openDashboardPageStyle.dashboardPreviewLineThickness;
    drawList.AddPolyline(outline.data(), static_cast<int>(outline.size()), color, ImDrawFlags_Closed, lineThickness);
    drawList.AddLine(backCorner, leftBottom, color, lineThickness);
    drawList.AddLine(backCorner, rightBottom, color, lineThickness);
    drawList.AddLine(backCorner, top, color, lineThickness);
}

void drawPreviewChart(ImDrawList& drawList, const DashboardPreview::Chart& chart, ImVec2 topLeft, ImVec2 bottomRight) {
    using enum DashboardPreview::ChartType;

    constexpr float kBorderInset       = 0.5f;
    const float     chartSignalPadding = LookAndFeel::instance().openDashboardPageStyle.dashboardPreviewChartPadding;
    const float     minSize            = chartSignalPadding * 2.f; // nothing could fit if smaller than this

    topLeft                   = {topLeft.x + kBorderInset, topLeft.y + kBorderInset};
    bottomRight               = {bottomRight.x - kBorderInset, bottomRight.y - kBorderInset};
    const float chartRounding = LookAndFeel::instance().openDashboardPageStyle.dashboardPreviewChartRounding;
    drawList.AddRectFilled(topLeft, bottomRight, ImGui::GetColorU32(ImGuiCol_FrameBg), chartRounding);
    drawList.AddRect(topLeft, bottomRight, ImGui::GetColorU32(ImGuiCol_Border), chartRounding);

    const float width  = bottomRight.x - topLeft.x - 2.f * chartSignalPadding;
    const float height = bottomRight.y - topLeft.y - 2.f * chartSignalPadding;
    if (width < minSize || height < minSize) {
        return;
    }
    const ImVec2 origin{topLeft.x + chartSignalPadding, topLeft.y + chartSignalPadding};

    // viridis, ImPlot's default colormap of spectrum history areas: its darkest, background colour
    const ImU32 spectrumHistoryColor = ImPlot::GetColormapColorU32(0, ImPlotColormap_Viridis);

    switch (chart.type) {
    case SpectrumDensity: {
        constexpr float kEmptyBottomArea = 1.f / 3.f;
        drawList.AddRectFilled(origin, {origin.x + width, origin.y + height}, spectrumHistoryColor, chartRounding);
        drawSignalLines(drawList, chart.signals, origin, {width, height * (1.f - kEmptyBottomArea)}, true);
        return;
    }
    case SpectrumView: {
        // top 1/3 is the spectrum with a waterfall below that
        constexpr float kSpectrumArea = 1.f / 3.f;
        const float     gap           = LookAndFeel::instance().openDashboardPageStyle.dashboardPreviewSpectrumWaterfallGap;

        const float spectrumHeight = std::max(0.f, (height - gap) * kSpectrumArea);
        drawList.AddRectFilled({origin.x, origin.y + spectrumHeight + gap}, {origin.x + width, origin.y + height}, spectrumHistoryColor, chartRounding);
        drawSignalLines(drawList, chart.signals, origin, {width, spectrumHeight}, true);
        return;
    }
    case WaterfallPlot: drawList.AddRectFilled(origin, {origin.x + width, origin.y + height}, spectrumHistoryColor, chartRounding); return;
    case SurfacePlot: drawSurfacePlotBoxIcon(drawList, origin, {width, height}); return;
    case SpectrumPlot: drawSignalLines(drawList, chart.signals, origin, {width, height}, true); return;
    case Chart2D:
    case Other: drawSignalLines(drawList, chart.signals, origin, {width, height}, false); return;
    }
}

} // namespace

std::optional<DashboardPreview> DashboardPreview::fromFlowgraphYaml(std::string_view flowgraphYaml) {
    const auto maybeYaml = gr::pmt::yaml::deserialize(flowgraphYaml);
    if (!maybeYaml) {
        return {};
    }
    const gr::property_map& yaml      = maybeYaml.value();
    const auto              dashboard = yaml.get_if<gr::property_map>("dashboard");
    if (!dashboard) {
        return {};
    }

    const FlowgraphSinks sinks = parseFlowgraphSinksFromYaml(yaml);

    StringMap<std::string> blockBySourceName;
    if (const auto sources = tryGetTensor(*dashboard, "sources")) {
        for (const auto& sourceValue : *sources) {
            if (const auto source = sourceValue.get_if<gr::property_map>()) {
                blockBySourceName.emplace(source->value_or<std::string>("name", std::string{}), source->value_or<std::string>("block", std::string{}));
            }
        }
    }

    DashboardPreview preview;

    StringMap<int>                           chartIndexByName;
    std::vector<std::array<std::int64_t, 4>> gridRects;
    const auto                               plots = tryGetTensor(*dashboard, "plots");
    if (!plots) {
        return {};
    }
    for (const auto& plotValue : *plots) {
        const auto plot = plotValue.get_if<gr::property_map>();
        if (!plot) {
            continue;
        }

        Chart             chart;
        const std::string typeName = plot->value_or<std::string>("type", std::string{});
        chart.type                 = typeName.empty() ? ChartType::Chart2D : chartTypeFromName(typeName); // XYChart is the default chart type

        if (const auto plotSources = tryGetTensor(*plot, "sources")) {
            for (const auto& sourceNameValue : *plotSources) {
                if (!sourceNameValue.is_string()) {
                    continue;
                }
                const std::string sourceName = sourceNameValue.value_or(std::string{});
                const auto        blockIt    = blockBySourceName.find(sourceName);
                const std::string blockName  = blockIt != blockBySourceName.end() ? blockIt->second : sourceName;
                if (const auto sinkIt = sinks.imPlotSinkDetailsByName.find(blockName); sinkIt != sinks.imPlotSinkDetailsByName.end()) {
                    const PlotSinkDetails& details = sinkIt->second;
                    chart.signals.push_back(details.hasInput ? details.signal : Signal{.isDataSet = details.signal.isDataSet});
                } else if (sinks.allBlockNames.contains(blockName)) {
                    // probably a user-defined sink
                    chart.signals.push_back({});
                }
            }
        }

        if (const auto rect = plot->find_value("rect")) {
            if (const auto rectView = rect->get_if<gr::TensorView<std::int64_t>>(); rectView && rectView->size() == 4UZ) {
                gridRects.push_back({(*rectView)[0], (*rectView)[1], (*rectView)[2], (*rectView)[3]});
            }
        }
        chartIndexByName.emplace(plot->value_or<std::string>("name", std::string{}), static_cast<int>(preview.charts.size()));
        preview.charts.push_back(std::move(chart));
    }
    if (preview.charts.empty()) {
        return {};
    }

    if (const auto windowLayout = dashboard->get_if<gr::property_map>("windowLayout")) {
        if (const auto dockSpace = windowLayout->get_if<gr::property_map>(dock_space::kDockSpace)) {
            parseDockedChartRectAreas(
                {
                    .inputYaml             = *dockSpace,
                    .inputChartIndexByName = chartIndexByName,
                    .outputPreview         = preview,
                },
                {.x = 0.f, .y = 0.f, .w = 1.f, .h = 1.f});
        }
        if (const auto floatingWindows = windowLayout->get_if<gr::property_map>(dock_space::kFloatingWindows)) {
            parseFloatingChartRectAreas({
                .inputYaml             = *floatingWindows,
                .inputChartIndexByName = chartIndexByName,
                .outputPreview         = preview,
            });
        }
    }
    if (std::ranges::none_of(preview.charts, [](const Chart& chart) { return chart.rect.has_value(); })) {
        auto                    layoutString = dashboard->value_or<std::string>("layout", std::string{});
        const DockingLayoutType layoutType   = magic_enum::enum_cast<DockingLayoutType>(layoutString, magic_enum::case_insensitive).value_or(DockingLayoutType::Grid);
        if (!assignChartRectAreasIfManuallySpecified(preview, layoutType, gridRects)) {
            assignChartRectAreasFromAutoLayoutRules(preview, layoutType);
        }
    }

    return preview;
}

void DashboardPreview::draw(ImVec2 topLeft, ImVec2 bottomRight) const {
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    drawList.PushClipRect(topLeft, bottomRight, true);

    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    const ImVec2 paneSize{bottomRight.x - topLeft.x, bottomRight.y - topLeft.y};
    for (const DashboardPreview::Chart& chart : charts) {
        if (!chart.rect) {
            continue;
        }
        DashboardPreview::Rect rect = *chart.rect;
        if (chart.isFloating) {
            if (viewportSize.x <= 0.f || viewportSize.y <= 0.f) {
                continue;
            }
            // floating windows have absolute rect coordinates; normalised, they fit only a screen of the size that created the dashboard
            rect = {.x = rect.x / viewportSize.x, .y = rect.y / viewportSize.y, .w = rect.w / viewportSize.x, .h = rect.h / viewportSize.y};
        }
        const ImVec2 chartMin{topLeft.x + rect.x * paneSize.x, topLeft.y + rect.y * paneSize.y};
        const ImVec2 chartMax{chartMin.x + rect.w * paneSize.x, chartMin.y + rect.h * paneSize.y};
        drawPreviewChart(drawList, chart, chartMin, chartMax);
    }

    drawList.PopClipRect();
}

} // namespace gr::od
