#include "DashboardPreview.hpp"

#include "../common/LookAndFeel.hpp"
#include "ColourManager.hpp"
#include "Docking.hpp"

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

namespace DigitizerUi {

namespace {

template<typename T>
T getAndConvertNumber(const gr::property_map& map, std::string_view key, T fallback) {
    const auto value = map.find_value(key);
    if (!value) {
        return fallback;
    }
    // if you pass a Value to convert_safely it says it is inconvertible and returns the fallback, you need to make it ValueView
    // TODO: remove this when a fix is in gnuradio, see https://github.com/fair-acc/gnuradio4/pull/817
    return gr::pmt::convert_safely<T>(static_cast<const gr::pmt::ValueView&>(*value)).value_or(fallback);
}

// there is no get_if overload for Tensor<Value> so emulate one by conditionally copying out of a TensorView
std::optional<gr::Tensor<gr::pmt::Value>> tryGetTensor(const gr::property_map& map, std::string_view key) {
    const auto value = map.find_value(key);
    if (!value) {
        return {};
    }
    if (const auto view = value->get_if<gr::TensorView<gr::pmt::Value>>()) {
        return view->owned();
    }
    return {};
}

/// Get the names of all blocks which have an incoming connection
std::unordered_set<std::string> getBlocksWithInput(const gr::property_map& flowgraphRoot) {
    std::unordered_set<std::string> blocksWithInput;
    const auto                      connections = tryGetTensor(flowgraphRoot, "connections");
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
    std::unordered_set<std::string>                  allBlockNames;
    std::unordered_map<std::string, PlotSinkDetails> imPlotSinkDetailsByName;
};

/// For every plot sink, get its color, whether it has inputs, and whether it is of a dataset type.
/// Colors being the same as in actual drawing relies on the actual loading code loading blocks in
/// the order they appear in the yaml, that being gr::detail::loadGraphFromMap()
FlowgraphSinks getFlowgraphSinks(const gr::property_map& flowgraphRoot) {
    FlowgraphSinks sinks;

    const auto blocks = tryGetTensor(flowgraphRoot, "blocks");
    if (!blocks) {
        return sinks;
    }
    const std::unordered_set<std::string> blocksWithInput = getBlocksWithInput(flowgraphRoot);

    opendigitizer::ColourManager manager;
    for (const auto& blockValue : *blocks) {
        const auto        block      = blockValue.value_or<gr::property_map>(gr::property_map{});
        const auto        parameters = block.value_or<gr::property_map>("parameters", gr::property_map{});
        const std::string name       = parameters.value_or<std::string>("name", std::string{});
        if (name.empty()) {
            continue;
        }
        sinks.allBlockNames.insert(name);

        const std::string id = block.value_or<std::string>("id", std::string{});
        if (!id.starts_with("opendigitizer::ImPlotSink")) {
            continue;
        }
        const std::uint32_t explicitColor = getAndConvertNumber<std::uint32_t>(parameters, "color", 0U);

        std::size_t slot = manager.getNextSlotIndex();
        manager.releaseSlotIndex(slot);
        slot = explicitColor == 0U ? manager.getNextSlotIndex() : manager.setColour(explicitColor);
        PlotSinkDetails details{
            .signal =
                {
                    .color     = manager.getColourAtSlot(manager.getActivePalette(), slot),
                    .isDataSet = id.starts_with("opendigitizer::ImPlotSink<gr::DataSet"),
                },
            .hasInput = blocksWithInput.contains(name),
        };
        sinks.imPlotSinkDetailsByName.emplace(name, details);
    }
    return sinks;
}

/// Somewhat duplicates some logic ImGuiDockSpaceState to go over all splits and resolve them into rect areas for a chart
void calculateDockedChartRects(DashboardPreview& preview, const gr::property_map& yaml, const std::unordered_map<std::string, int>& chartIndexByName, DashboardPreview::Rect area) {
    const auto hsplit = yaml.get_if<gr::property_map>("hsplit");
    const auto vsplit = yaml.get_if<gr::property_map>("vsplit");
    if (!hsplit && !vsplit) {
        return;
    }
    const gr::property_map& inner = hsplit ? *hsplit : *vsplit;
    const float             ratio = std::clamp(getAndConvertNumber(inner, "ratio", 1.f), 0.05f, 0.95f);

    const auto placeChild = [&](std::string_view key, DashboardPreview::Rect childArea) {
        const auto child = inner.find_value(key);
        if (!child) {
            return;
        }
        if (child->is_string()) {
            // leaf
            if (const auto chartIt = chartIndexByName.find(child->value_or(std::string{})); chartIt != chartIndexByName.end()) {
                preview.charts[static_cast<std::size_t>(chartIt->second)].rect = childArea;
            }
        } else if (const auto childMap = child->get_if<gr::property_map>()) {
            // branch
            calculateDockedChartRects(preview, *childMap, chartIndexByName, childArea);
        }
    };

    // first: left/top
    // second: right/bottom
    // value of ratio goes to the left node in hsplit or the bottom node in vsplit
    // TODO: fix this, Docking.cpp, and the dashboards so that `ratio` goes to the top instead
    // of bottom with vsplit, since that matches the priority order that things get filled in
    if (hsplit) {
        placeChild("first", {.x = area.x, .y = area.y, .w = area.w * ratio, .h = area.h});
        placeChild("second", {.x = area.x + area.w * ratio, .y = area.y, .w = area.w * (1.f - ratio), .h = area.h});
    } else {
        placeChild("first", {.x = area.x, .y = area.y, .w = area.w, .h = area.h * (1.f - ratio)});
        placeChild("second", {.x = area.x, .y = area.y + area.h * (1.f - ratio), .w = area.w, .h = area.h * ratio});
    }
}

/// Save the values from the floating window rects into the Chart. these values will be normalized during drawing
void calculateFloatingChartRects(DashboardPreview& preview, const gr::property_map& floatingWindows, const std::unordered_map<std::string, int>& chartIndexByName) {
    for (const auto& [windowName, windowValue] : floatingWindows) {
        const auto chartIt = chartIndexByName.find(std::string(windowName));
        if (chartIt == chartIndexByName.end()) {
            continue;
        }
        const auto window = windowValue.get_if<gr::property_map>();
        if (!window) {
            continue;
        }
        DashboardPreview::Chart& chart = preview.charts[static_cast<std::size_t>(chartIt->second)];
        chart.isFloating               = true;

        chart.rect = {
            .x = getAndConvertNumber(*window, "x", 0.f),
            .y = getAndConvertNumber(*window, "y", 0.f),
            .w = getAndConvertNumber(*window, "width", 0.f),
            .h = getAndConvertNumber(*window, "height", 0.f),
        };
    }
}

/// If we are in the free layout and there are exact window positions for all the charts specified, use those as their positions.
/// returns true if the grid rects were indeed specified and we are in free layout
[[nodiscard]] bool assignExactRectsIfSpecified(DashboardPreview& preview, DockingLayoutType layoutType, const std::vector<std::array<std::int64_t, 4>>& gridRects) {
    const std::size_t numCharts = preview.charts.size();
    if (numCharts == 0UZ || numCharts != gridRects.size() || layoutType != DockingLayoutType::Free) {
        return false;
    }

    std::int64_t maxX = 1;
    std::int64_t maxY = 1;
    for (const auto& rect : gridRects) {
        maxX = std::max(maxX, rect[0] + rect[2]);
        maxY = std::max(maxY, rect[1] + rect[3]);
    }
    for (std::size_t i = 0UZ; i < numCharts; ++i) {
        const auto& rect       = gridRects[i];
        preview.charts[i].rect = DashboardPreview::Rect{
            .x = static_cast<float>(rect[0]) / static_cast<float>(maxX),
            .y = static_cast<float>(rect[1]) / static_cast<float>(maxY),
            .w = static_cast<float>(rect[2]) / static_cast<float>(maxX),
            .h = static_cast<float>(rect[3]) / static_cast<float>(maxY),
        };
    }
    return true;
}

/// Predict how auto-layout will place the charts if no rects or splits are
/// specified.
/// also see: DockSpace::layoutInExactFree(), DockSpace::layoutInBox(),
/// DockSpace::layoutInGrid()
void calculateAutoLaidOutChartRects(DashboardPreview& preview, DockingLayoutType layoutType) {
    const std::size_t n = preview.charts.size();
    if (n == 0UZ) {
        return;
    }

    const auto cellsPerAxis = [n](bool isRow, bool isColumn) -> std::pair<std::size_t, std::size_t> {
        if (isRow) {
            return {n, 1UZ};
        }
        if (isColumn) {
            return {1UZ, n};
        }
        const std::size_t columns = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(n))));
        return {columns, (n + columns - 1UZ) / columns};
    };
    const auto [columns, rows] = cellsPerAxis(layoutType == DockingLayoutType::Row, layoutType == DockingLayoutType::Column);

    for (std::size_t i = 0UZ; i < n; ++i) {
        const std::size_t column = i % columns;
        const std::size_t row    = i / columns;
        const float       x      = static_cast<float>(column) / static_cast<float>(columns);
        preview.charts[i].rect   = DashboardPreview::Rect{
              .x = x,
              .y = static_cast<float>(row) / static_cast<float>(rows),
            // layoutInGrid docks the last window into whatever remains of its row
              .w = i + 1UZ == n ? 1.f - x : 1.f / static_cast<float>(columns),
              .h = 1.f / static_cast<float>(rows),
        };
    }
}

DashboardPreview::ChartType chartTypeFromName(std::string_view typeName) {
    using enum DashboardPreview::ChartType;
    if (typeName.contains("XYChart") || typeName.contains("YYChart")) {
        return Chart2D;
    }
    for (const auto& [chartType, name] : magic_enum::enum_entries<DashboardPreview::ChartType>()) {
        if (typeName.contains(name)) {
            return chartType;
        }
    }
    return Other;
}

ImU32 signalColor(const DashboardPreview::Signal& signal) { return signal.color == 0U ? ImGui::GetColorU32(ImGuiCol_PlotLines) : rgbToImGuiABGR(signal.color); }

[[nodiscard]] float spectrumPointHeight(float pointIndex, float t) {
    constexpr float minHeight             = 0.06f;
    constexpr float maxHeight             = 0.95f;
    constexpr float startingPeakMinOffset = 0.15f;
    constexpr float startingPeakWidth     = 0.5f;
    constexpr float peakSpacing           = 0.618f; // wraps around, so this is good for preventing overlap
    struct Peak {
        float offset;
        float height;
        float harshness;
    };
    constexpr std::array peaks = {
        Peak{.offset = 0.f, .height = 0.8f, .harshness = 250.f},
        Peak{.offset = 0.33f, .height = 0.5f, .harshness = 350.f},
        Peak{.offset = 0.57f, .height = 0.3f, .harshness = 450.f},
    };

    // sum the effect of each peak on this point
    const float mainPeak = startingPeakMinOffset + startingPeakWidth * std::fmod(pointIndex * peakSpacing, 1.f);
    float       value    = minHeight;
    for (const Peak& peak : peaks) {
        const float position = std::fmod(mainPeak + peak.offset, 1.f);
        value += peak.height * std::exp(-peak.harshness * (t - position) * (t - position));
    }

    return 1.f - std::min(value, maxHeight); // y is down
}

[[nodiscard]] float sineWavePointHeight(float pointIndex, float t) {
    constexpr float basePeriod   = 1.5f;
    constexpr float periodOffset = 0.5f;
    constexpr float phaseOffset  = 1.9f;
    constexpr float amplitude    = 0.32f; // decently big but still < 1 so its within the chart area

    const float periods = basePeriod + periodOffset * pointIndex;
    return 0.5f - amplitude * std::sin(2.f * std::numbers::pi_v<float> * periods * t + phaseOffset * pointIndex);
}

/// Draw some lines either to look like a sine wave or spectrum, depending on if the type of the plot sink is a gr::DataSet or not
void drawSignalLines(ImDrawList& drawList, std::span<const DashboardPreview::Signal> signals, ImVec2 origin, ImVec2 size, bool forceSpectrumStyle) {
    constexpr float lineThickness = 1.f;
    constexpr int   numPoints     = 24;

    std::array<ImVec2, numPoints> points{};
    for (std::size_t signalIndex = 0UZ; signalIndex < signals.size(); ++signalIndex) {
        const float indexFloat    = static_cast<float>(signalIndex);
        const bool  spectrumStyle = forceSpectrumStyle || signals[signalIndex].isDataSet;
        for (std::size_t point = 0; point < numPoints; ++point) {
            const float t = static_cast<float>(point) / static_cast<float>(numPoints - 1);
            const float y = spectrumStyle ? spectrumPointHeight(indexFloat, t) : sineWavePointHeight(indexFloat, t);
            points[point] = {origin.x + t * size.x, origin.y + y * size.y};
        }
        drawList.AddPolyline(points.data(), numPoints, signalColor(signals[signalIndex]), ImDrawFlags_None, lineThickness);
    }
}

/// Draw something that looks like the 3D box of a surface plot
/// Could probably be an icon...
void drawSurfacePlotBoxIcon(ImDrawList& drawList, ImVec2 origin, ImVec2 size) {
    constexpr float sideHalfHeightPercent = 0.3f;  // length of flat sides / 2
    constexpr float pointOffsetPercent    = 0.11f; // length of pointy triangles of hexagon

    const float  unit           = std::min(size.x, size.y);
    const ImVec2 center         = {origin.x + 0.5f * size.x, origin.y + 0.5f * size.y};
    const float  halfWidth      = (sideHalfHeightPercent + pointOffsetPercent) * unit;
    const float  sideHalfHeight = sideHalfHeightPercent * unit;
    const float  pointOffset    = pointOffsetPercent * unit;

    const ImU32      color   = ImGui::GetColorU32(ImGuiCol_PlotLines);
    const std::array outline = {
        ImVec2{center.x, center.y - sideHalfHeight - pointOffset}, // top
        ImVec2{center.x + halfWidth, center.y - sideHalfHeight},   // right top
        ImVec2{center.x + halfWidth, center.y + sideHalfHeight},   // right bottom
        ImVec2{center.x, center.y + sideHalfHeight + pointOffset}, // bottom
        ImVec2{center.x - halfWidth, center.y + sideHalfHeight},   // left bottom
        ImVec2{center.x - halfWidth, center.y - sideHalfHeight},   // left top
    };
    const ImVec2 backCorner{center.x, center.y + sideHalfHeight - pointOffset};
    const ImVec2 leftBottom  = outline[4];
    const ImVec2 rightBottom = outline[2];
    const ImVec2 top         = outline[0];

    constexpr float lineThickness = 1.f;
    drawList.AddPolyline(outline.data(), static_cast<int>(outline.size()), color, ImDrawFlags_Closed, lineThickness);
    drawList.AddLine(backCorner, leftBottom, color, lineThickness);
    drawList.AddLine(backCorner, rightBottom, color, lineThickness);
    drawList.AddLine(backCorner, top, color, lineThickness);
}

void drawPreviewChart(ImDrawList& drawList, const DashboardPreview::Chart& chart, ImVec2 topLeft, ImVec2 bottomRight) {
    using enum DashboardPreview::ChartType;

    constexpr float borderInset        = 0.5f;
    constexpr float chartSignalPadding = 2.f;
    constexpr float minSize            = chartSignalPadding * 2.f; // nothing could fit if smaller than this

    topLeft     = {topLeft.x + borderInset, topLeft.y + borderInset};
    bottomRight = {bottomRight.x - borderInset, bottomRight.y - borderInset};
    drawList.AddRectFilled(topLeft, bottomRight, ImGui::GetColorU32(ImGuiCol_FrameBg), 0.f);
    drawList.AddRect(topLeft, bottomRight, ImGui::GetColorU32(ImGuiCol_Border), 0.f);

    const float width  = bottomRight.x - topLeft.x - 2.f * chartSignalPadding;
    const float height = bottomRight.y - topLeft.y - 2.f * chartSignalPadding;
    if (width < minSize || height < minSize) {
        return;
    }
    const ImVec2 origin{topLeft.x + chartSignalPadding, topLeft.y + chartSignalPadding};

    // viridis is the default implot colormap for spectrum history areas, get its darkest / background color
    const ImU32 spectrumHistoryColor = ImPlot::GetColormapColorU32(0, ImPlotColormap_Viridis);

    switch (chart.type) {
    case SpectrumDensity: {
        constexpr float emptyBottomArea = 1.f / 3.f;
        drawList.AddRectFilled(origin, {origin.x + width, origin.y + height}, spectrumHistoryColor, 0.f);
        drawSignalLines(drawList, chart.signals, origin, {width, height * (1.f - emptyBottomArea)}, true);
        return;
    }
    case SpectrumView: {
        // top 1/3 is the spectrum with a waterfall below that
        constexpr float spectrumArea = 1.f / 3.f;
        constexpr float gap          = 2.f;

        const float spectrumHeight = std::max(0.f, (height - gap) * spectrumArea);
        drawList.AddRectFilled({origin.x, origin.y + spectrumHeight + gap}, {origin.x + width, origin.y + height}, spectrumHistoryColor, 0.f);
        drawSignalLines(drawList, chart.signals, origin, {width, spectrumHeight}, true);
        return;
    }
    case WaterfallPlot: drawList.AddRectFilled(origin, {origin.x + width, origin.y + height}, spectrumHistoryColor, 0.f); return;
    case SurfacePlot: drawSurfacePlotBoxIcon(drawList, origin, {width, height}); return;
    case SpectrumPlot: drawSignalLines(drawList, chart.signals, origin, {width, height}, true); return;
    case Chart2D:
    case Other: drawSignalLines(drawList, chart.signals, origin, {width, height}, false); return;
    }
}

} // namespace

/// For this function, also see Dashboard::doLoad() in Dashboard.cpp, which has some similarities as it also needs to parse dashboard yaml
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

    const FlowgraphSinks sinks = getFlowgraphSinks(yaml);

    std::unordered_map<std::string, std::string> blockBySourceName;
    if (const auto sources = tryGetTensor(*dashboard, "sources")) {
        for (const auto& sourceValue : *sources) {
            if (const auto source = sourceValue.get_if<gr::property_map>()) {
                blockBySourceName.emplace(source->value_or<std::string>("name", std::string{}), source->value_or<std::string>("block", std::string{}));
            }
        }
    }

    DashboardPreview preview;

    std::unordered_map<std::string, int>     chartIndexByName;
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
                    if (sinkIt->second.hasInput) {
                        chart.signals.push_back(sinkIt->second.signal);
                    }
                } else if (sinks.allBlockNames.contains(blockName)) {
                    // probably some user defined sink, just draw a sine wave
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
        if (const auto dockSpace = windowLayout->get_if<gr::property_map>("dockSpace")) {
            calculateDockedChartRects(preview, *dockSpace, chartIndexByName, {.x = 0.f, .y = 0.f, .w = 1.f, .h = 1.f});
        }
        if (const auto floatingWindows = windowLayout->get_if<gr::property_map>("floatingWindows")) {
            calculateFloatingChartRects(preview, *floatingWindows, chartIndexByName);
        }
    }
    if (std::ranges::none_of(preview.charts, [](const Chart& chart) { return chart.rect.has_value(); })) {
        auto                    layoutString = dashboard->value_or<std::string>("layout", std::string{});
        const DockingLayoutType layoutType   = magic_enum::enum_cast<DockingLayoutType>(layoutString, magic_enum::case_insensitive).value_or(DockingLayoutType::Grid);
        if (!assignExactRectsIfSpecified(preview, layoutType, gridRects)) {
            calculateAutoLaidOutChartRects(preview, layoutType);
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
            // normalize floating windows because their rect coordinates are absolute. only really makes sense if the current screen is the same size as the screen that created the dashboard.
            // TODO: store floating window coordinates as relative coordinates in yaml so that charts look the same on all screen sizes
            rect = {.x = rect.x / viewportSize.x, .y = rect.y / viewportSize.y, .w = rect.w / viewportSize.x, .h = rect.h / viewportSize.y};
        }
        const ImVec2 chartMin{topLeft.x + rect.x * paneSize.x, topLeft.y + rect.y * paneSize.y};
        const ImVec2 chartMax{chartMin.x + rect.w * paneSize.x, chartMin.y + rect.h * paneSize.y};
        drawPreviewChart(drawList, chart, chartMin, chartMax);
    }

    drawList.PopClipRect();
}

} // namespace DigitizerUi
