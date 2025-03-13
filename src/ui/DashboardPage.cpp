#include "DashboardPage.hpp"

#include <fmt/format.h>
#include <implot.h>

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"
#include "common/TouchHandler.hpp"
#include "conversion.hpp"

#include "components//ImGuiNotify.hpp"
#include "components/Splitter.hpp"

#include "blocks/ImPlotSink.hpp"

namespace DigitizerUi {

namespace {
constexpr inline auto kMaxPlots   = 16u;
constexpr inline auto kGridWidth  = 16u;
constexpr inline auto kGridHeight = 16u;

// Holds quantity + unit
struct AxisCategory {
    std::string quantity;
    std::string unit;
    uint32_t    color = 0xAAAAAA;
    AxisScale   scale = AxisScale::Linear;
};

std::optional<std::size_t> findOrCreateCategory(std::array<std::optional<AxisCategory>, 3>& cats, std::string_view qStr, std::string_view uStr, uint32_t colorDef) {
    for (std::size_t i = 0UZ; i < cats.size(); ++i) {
        // see if it already exists
        if (cats[i].has_value()) {
            const auto& cat = cats[i].value();
            if (cat.quantity == qStr && cat.unit == uStr) {
                return static_cast<int>(i); // found match
            }
        }
    }

    for (std::size_t i = 0UZ; i < cats.size(); ++i) {
        // else try to open a new slot
        if (!cats[i].has_value()) {
            cats[i] = AxisCategory{.quantity = std::string(qStr), .unit = std::string(uStr), .color = colorDef};
            return i;
        }
    }
    return std::nullopt; // no slot left
}

void assignSourcesToAxes(const Dashboard::Plot& plot, Dashboard& /*dashboard*/, std::array<std::optional<AxisCategory>, 3>& xCats, std::array<std::vector<std::string>, 3>& xAxisGroups, std::array<std::optional<AxisCategory>, 3>& yCats, std::array<std::vector<std::string>, 3>& yAxisGroups) {
    enum class AxisKind { X = 0, Y };

    xCats.fill(std::nullopt);
    yCats.fill(std::nullopt);
    for (auto& g : xAxisGroups) {
        g.clear();
    }
    for (auto& g : yAxisGroups) {
        g.clear();
    }

    for (const auto& source : plot.sources) {
        auto grBlock = opendigitizer::ImPlotSinkManager::instance().findSink([source](auto& block) { return block.name() == source->name(); });
        if (!grBlock) {
            continue;
        }

        // quantity, unit
        std::string qStr, uStr;
        if (auto qOpt = grBlock->settings().get("signal_quantity")) {
            qStr = std::get<std::string>(*qOpt);
        }
        if (auto uOpt = grBlock->settings().get("signal_unit")) {
            uStr = std::get<std::string>(*uOpt);
        }

        // axis kind = X or Y
        AxisKind axisKind = AxisKind::Y; // default
        if (auto axisOpt = grBlock->settings().get("signal_axis")) {
            std::string axisVal = std::get<std::string>(*axisOpt);
            if (axisVal == "X") {
                axisKind = AxisKind::X;
            }
        }

        auto color = ImGui::ColorConvertFloat4ToU32(source->color());
        if (auto idx = findOrCreateCategory(axisKind == AxisKind::X ? xCats : yCats, qStr, uStr, color); idx) {
            if (axisKind == AxisKind::X) {
                xAxisGroups[idx.value()].push_back(source->name());
            } else {
                yAxisGroups[idx.value()].push_back(source->name());
            }
        } else {
            components::Notification::warning(fmt::format("No free slots for {} axis. Ignoring source '{}' (q='{}', u='{}')\n", (axisKind == AxisKind::X ? "X" : "Y"), source->name(), qStr, uStr));
            continue;
        }
    }
}

std::string buildLabel(const std::optional<AxisCategory>& catOpt, std::size_t idx, bool isX) {
    if (!catOpt.has_value()) {
        // fallback
        return isX ? "time" : "y-axis [a.u.]";
    }
    const auto& cat = catOpt.value();
    if (!cat.quantity.empty() && !cat.unit.empty()) {
        return fmt::format("{} [{}]", cat.quantity, cat.unit);
    }
    if (!cat.quantity.empty()) {
        return cat.quantity;
    }
    if (!cat.unit.empty()) {
        return cat.unit;
    }
    return fmt::format("{}-axis #{}", (isX ? "X" : "Y"), idx + 1UZ); // fallback if both empty
}

void setupPlotAxes(Dashboard::Plot& plot, const std::array<std::optional<AxisCategory>, 3>& xCats, const std::array<std::optional<AxisCategory>, 3>& yCats) {
    using Axis = Dashboard::Plot::AxisKind;

    static constexpr ImAxis kXs[3] = {ImAxis_X1, ImAxis_X2, ImAxis_X3};
    static constexpr ImAxis kYs[3] = {ImAxis_Y1, ImAxis_Y2, ImAxis_Y3};

    auto colorU32toImVec4 = [](uint32_t c) -> ImVec4 {
        const float r = float((c >> 16) & 0xFF) / 255.f;
        const float g = float((c >> 8) & 0xFF) / 255.f;
        const float b = float((c >> 0) & 0xFF) / 255.f;
        return ImVec4(r, g, b, 1.f);
    };

    auto truncateLabel = [&](std::string_view original, float availableWidth) -> std::string {
        // truncate label if it won't fit into the available `width`
        const float textWidth = ImGui::CalcTextSize(original.data()).x;
        if (textWidth <= availableWidth) {
            return std::string(original);
        }

        static const float ellipsisWidth = ImGui::CalcTextSize("...").x;
        if (availableWidth <= ellipsisWidth + 1.0f) {
            // not enough space for a partial string
            return "...";
        }

        // shrink text to fit and prepend "..."
        float       scaleFactor  = (availableWidth - ellipsisWidth) / std::max(1.0f, textWidth);
        std::size_t fitCharCount = static_cast<std::size_t>(std::floor(scaleFactor * static_cast<float>(original.size())));

        std::string truncated(original);
        if (fitCharCount < truncated.size()) {
            truncated = truncated.substr(truncated.size() - fitCharCount);
        }
        return fmt::format("...{}", truncated);
    };

    auto setAxisScale = [](ImAxis axisID, AxisScale scale) {
        using enum AxisScale;
        switch (scale) {

        case Log10: ImPlot::SetupAxisScale(axisID, ImPlotScale_Log10); return;
        case SymLog: ImPlot::SetupAxisScale(axisID, ImPlotScale_SymLog); return;
        case Time: {
            ImPlot::SetupAxisScale(axisID, ImPlotScale_Time);
            ImPlot::GetStyle().UseISO8601     = true;
            ImPlot::GetStyle().Use24HourClock = true;
        }
            return;
        case Linear:
        case LinearReverse:
        default: ImPlot::SetupAxisScale(axisID, ImPlotScale_Linear);
        }
    };

    for (auto& [axisType, minVal, maxVal, scale, width] : plot.axes) {
        const bool   isX    = (axisType == Axis::X);
        const ImAxis axisId = isX ? ImAxis_X1 : ImAxis_Y1;

        // compute flags
        ImPlotAxisFlags flags     = ImPlotAxisFlags_None;
        bool            finiteMin = std::isfinite(minVal);
        bool            finiteMax = std::isfinite(maxVal);
        if (finiteMin && !finiteMax) {
            flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_LockMin;
        } else if (!finiteMin && finiteMax) {
            flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_LockMax;
        } else if (!finiteMin && !finiteMax) {
            flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
        } // else both limits set -> no auto-fit

        if (isX && scale == AxisScale::Time) {
            flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
        }

        const std::string axisLabel = truncateLabel(buildLabel(isX ? xCats[0] : yCats[0], 0UZ, isX), width);

        if (!isX && yCats[0].has_value()) {
            ImVec4 col = colorU32toImVec4(yCats[0]->color);
            // Colour the text & label
            ImPlot::PushStyleColor(ImPlotCol_AxisText, col);
            ImPlot::PushStyleColor(ImPlotCol_AxisTick, col);
        }

        ImPlot::SetupAxis(axisId, axisLabel.c_str(), flags);
        if (finiteMin && finiteMax) {
            ImPlot::SetupAxisLimits(axisId, static_cast<double>(minVal), static_cast<double>(maxVal)); // TODO check and change axis range definitions to double
        }

        setAxisScale(axisId, scale);

        if (!isX && yCats[0].has_value()) {
            ImPlot::PopStyleColor(2);
        }
    }

    for (std::size_t i = 1UZ; i < xCats.size(); ++i) {
        // set up X2/X3 from xCats[1..2]
        if (!xCats[i].has_value()) {
            continue;
        }
        ImPlotAxisFlags flags = ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_Opposite; // on top
        std::string     lbl   = buildLabel(xCats[i], i, /*isX=*/true);
        ImPlot::SetupAxis(kXs[i], lbl.c_str(), flags);
        setAxisScale(kXs[i], xCats[i]->scale);
    }

    for (std::size_t i = 1UZ; i < yCats.size(); ++i) {
        // set up Y2/Y3 from yCats[1..2]
        if (!yCats[i].has_value()) {
            continue;
        }
        ImVec4 col = colorU32toImVec4(yCats[i]->color);
        ImPlot::PushStyleColor(ImPlotCol_AxisText, col);
        ImPlot::PushStyleColor(ImPlotCol_AxisTick, col);
        ImPlotAxisFlags flags = ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_Opposite; // on right
        std::string     lbl   = buildLabel(yCats[i], i, /*isX=*/false);
        ImPlot::SetupAxis(kYs[i], lbl.c_str(), flags);
        ImPlot::PopStyleColor(2);
        setAxisScale(kYs[i], yCats[i]->scale);
    }
}
} // namespace

static bool plotButton(const char* glyph, const char* tooltip) noexcept {
    const bool ret = [&] {
        IMW::StyleColor normal(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        IMW::StyleColor hovered(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.1f));
        IMW::StyleColor active(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0.2f));
        IMW::Font       font(LookAndFeel::instance().fontIconsSolid);
        return ImGui::Button(glyph);
    }();

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }

    return ret;
}

static void alignForWidth(float width, float alignment = 0.5f) noexcept {
    float avail = ImGui::GetContentRegionAvail().x;
    float off   = (avail - width) * alignment;
    if (off > 0.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
    }
}

// Draw the multi-axis plot
void DashboardPage::drawPlot(Dashboard& dashboard, Dashboard::Plot& plot) noexcept {
    // 1) Build up two sets of categories for X & Y
    std::array<std::optional<AxisCategory>, 3> xCats{};
    std::array<std::vector<std::string>, 3>    xAxisGroups{};
    std::array<std::optional<AxisCategory>, 3> yCats{};
    std::array<std::vector<std::string>, 3>    yAxisGroups{};

    assignSourcesToAxes(plot, dashboard, xCats, xAxisGroups, yCats, yAxisGroups);

    // 2) Setup up to 3 X axes & 3 Y axes
    setupPlotAxes(plot, xCats, yCats);
    ImPlot::SetupFinish();

    // compute axis pixel width or height
    {
        const ImPlotRect axisLimits = ImPlot::GetPlotLimits(IMPLOT_AUTO, IMPLOT_AUTO);
        const ImVec2     p0         = ImPlot::PlotToPixels(axisLimits.X.Min, axisLimits.Y.Min);
        const ImVec2     p1         = ImPlot::PlotToPixels(axisLimits.X.Max, axisLimits.Y.Max);
        const float      xWidth     = std::round(std::abs(p1.x - p0.x));
        const float      yHeight    = std::round(std::abs(p1.y - p0.y));
        std::ranges::for_each(plot.axes, [xWidth, yHeight](auto& a) { a.width = (a.axis == Dashboard::Plot::AxisKind::X) ? xWidth : yHeight; });
    }

    // draw each source on the correct axis
    bool drawTag = true;
    for (opendigitizer::ImPlotSinkModel* sourceBlock : plot.sources) {
        if (!sourceBlock->isVisible) {
            // consume data if hidden
            std::ignore = sourceBlock->work(std::numeric_limits<std::size_t>::max());
            continue;
        }

        const auto& sourceBlockName = sourceBlock->name();

        // figure out which axis group check X first
        std::size_t xAxisID = std::numeric_limits<std::size_t>::max();
        for (std::size_t i = 0UZ; i < 3UZ && xAxisID == std::numeric_limits<std::size_t>::max(); ++i) {
            auto it = std::ranges::find(xAxisGroups[i], sourceBlock->name());
            if (it != xAxisGroups[i].end()) {
                xAxisID = i;
            }
        }
        if (xAxisID == std::numeric_limits<std::size_t>::max()) { // default to X0 if not found
            xAxisID = 0UZ;
        }

        // check Y if not found
        std::size_t yAxisID = std::numeric_limits<std::size_t>::max();
        for (std::size_t i = 0UZ; i < 3UZ && yAxisID == std::numeric_limits<std::size_t>::max(); ++i) {
            auto it = std::ranges::find(yAxisGroups[i], sourceBlock->name());
            if (it != yAxisGroups[i].end()) {
                yAxisID = i;
            }
        }
        if (yAxisID == std::numeric_limits<std::size_t>::max()) { // default to Y0 if not found
            yAxisID = 0;
        }

        std::ignore = sourceBlock->draw({{"draw_tag", drawTag}, {"xAxisID", xAxisID}, {"yAxisID", yAxisID}, {"scale", std::string(magic_enum::enum_name(plot.axes[0].scale))}});
        drawTag     = false;

        // allow legend item labels to be DND sources
        if (ImPlot::BeginDragDropSourceItem(sourceBlockName.c_str())) {
            DigitizerUi::DashboardPage::DndItem dnd = {&plot, sourceBlock};
            ImGui::SetDragDropPayload(dnd_type, &dnd, sizeof(dnd));

            ImPlot::ItemIcon(sourceBlock->color());
            ImGui::SameLine();
            ImGui::TextUnformatted(sourceBlockName.c_str());
            ImPlot::EndDragDropSource();
        }
    }
}

void DashboardPage::draw(Dashboard& dashboard, Mode mode) noexcept {
    const float  left = ImGui::GetCursorPosX();
    const float  top  = ImGui::GetCursorPosY();
    const ImVec2 size = ImGui::GetContentRegionAvail();

    const bool      horizontalSplit   = size.x > size.y;
    constexpr float splitterWidth     = 6;
    constexpr float halfSplitterWidth = splitterWidth / 2.f;
    const float     ratio             = components::Splitter(size, horizontalSplit, splitterWidth, 0.2f, !m_editPane.block);

    ImGui::SetCursorPosX(left);
    ImGui::SetCursorPosY(top);

    {
        IMW::Child plotsChild("##plots", horizontalSplit ? ImVec2(size.x * (1.f - ratio) - halfSplitterWidth, size.y) : ImVec2(size.x, size.y * (1.f - ratio) - halfSplitterWidth), false, ImGuiWindowFlags_NoScrollbar);

        if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_editPane.block = nullptr;
        }

        // Plots
        {
            IMW::Group group;
            drawPlots(dashboard, mode);
        }
        ImGui::SetCursorPos(ImVec2(0, ImGui::GetWindowHeight() - legend_box.y));

        // Legend
        {
            IMW::Group group;
            // Button strip
            if (mode == Mode::Layout) {
                if (plotButton("\uF201", "create new chart")) {
                    // chart-line
                    newPlot(dashboard);
                }
                ImGui::SameLine();
                if (plotButton("\uF7A5", "change to the horizontal layout")) {
                    m_dockSpace.setLayoutType(DockingLayoutType::Row);
                }
                ImGui::SameLine();
                if (plotButton("\uF7A4", "change to the vertical layout")) {
                    m_dockSpace.setLayoutType(DockingLayoutType::Column);
                }
                ImGui::SameLine();
                if (plotButton("\uF58D", "change to the grid layout")) {
                    m_dockSpace.setLayoutType(DockingLayoutType::Grid);
                }
                ImGui::SameLine();
                if (plotButton("\uF248", "change to the free layout")) {
                    m_dockSpace.setLayoutType(DockingLayoutType::Free);
                }
                ImGui::SameLine();
            }

            drawLegend(dashboard, mode);

            // Post button strip
            if (mode == Mode::Layout) {
                ImGui::SameLine();
                if (plotButton("\uf067", "add signal") && m_signalSelector) {
                    // plus
                    // add new signal
                    m_signalSelector->open();
                }
            }

            if (m_signalSelector) {
                m_signalSelector->draw();
            }

            if (LookAndFeel::instance().prototypeMode) {
                ImGui::SameLine();
                // Retrieve FPS and milliseconds per iteration
                const float fps     = ImGui::GetIO().Framerate;
                const auto  str     = fmt::format("FPS:{:5.0f}({:2}ms)", fps, LookAndFeel::instance().execTime.count());
                const auto  estSize = ImGui::CalcTextSize(str.c_str());
                alignForWidth(estSize.x, 1.0);
                ImGui::Text("%s", str.c_str());
            }
        }
        legend_box.y = std::floor(ImGui::GetItemRectSize().y * 1.5f);
    }

    if (horizontalSplit) {
        const float w = size.x * ratio;
        components::BlockControlsPanel(m_editPane, {left + size.x - w + halfSplitterWidth, top}, {w - halfSplitterWidth, size.y}, true);
    } else {
        const float h = size.y * ratio;
        components::BlockControlsPanel(m_editPane, {left, top + size.y - h + halfSplitterWidth}, {size.x, h - halfSplitterWidth}, false);
    }
}

void DashboardPage::drawPlots(Dashboard& dashboard, DigitizerUi::DashboardPage::Mode mode) {
    pane_size = ImGui::GetContentRegionAvail();
    pane_size.y -= legend_box.y;

    const float w = pane_size.x / float(kGridWidth);
    const float h = pane_size.y / float(kGridHeight);

    if (mode == Mode::Layout) {
        drawGrid(w, h);
    }

    Dashboard::Plot* toDelete = nullptr;

    DockSpace::Windows windows;
    auto&              plots = dashboard.plots();
    windows.reserve(plots.size());

    for (auto& plot : plots) {
        windows.push_back(plot.window);
        plot.window->renderFunc = [this, &dashboard, &plot, mode] {
            const float offset = (mode == Mode::Layout) ? 5.f : 0.f;

            const bool  showTitle = false; // TODO: make this and the title itself a configurable/editable entity
            ImPlotFlags plotFlags = ImPlotFlags_NoChild;
            plotFlags |= showTitle ? ImPlotFlags_None : ImPlotFlags_NoTitle;
            plotFlags |= mode == Mode::Layout ? ImPlotFlags_None : ImPlotFlags_NoLegend;

            ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2{0, 0}); // TODO: make this perhaps a global style setting via ImPlot::GetStyle()
            ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding, ImVec2{3, 1});
            auto plotSize = ImGui::GetContentRegionAvail();
            ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0.00f, 0.05f));
            if (TouchHandler<>::BeginZoomablePlot(plot.name, plotSize - ImVec2(2 * offset, 2 * offset), plotFlags)) {
                drawPlot(dashboard, plot);

                // allow the main plot area to be a DND target
                if (ImPlot::BeginDragDropTargetPlot()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(dnd_type)) {
                        auto* dnd = static_cast<DndItem*>(payload->Data);
                        plot.sources.push_back(dnd->plotSource);
                        if (auto* dndPlot = dnd->plot) {
                            dndPlot->sources.erase(std::ranges::find(dndPlot->sources, dnd->plotSource));
                        }
                    }
                    ImPlot::EndDragDropTarget();
                }

                // update plot.axes if auto-fit is not used
                ImPlotRect rect = ImPlot::GetPlotLimits();
                for (auto& a : plot.axes) {
                    const bool      isX              = (a.axis == Dashboard::Plot::AxisKind::X);
                    const ImAxis    axisId           = isX ? ImAxis_X1 : ImAxis_Y1; // TODO multi-axis extension
                    ImPlotAxisFlags axisFlags        = ImPlot::GetCurrentPlot()->Axes[axisId].Flags;
                    bool            isAutoOrRangeFit = (axisFlags & (ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit)) != 0;
                    if (!isAutoOrRangeFit) {
                        // store back min/max from the actual final plot region
                        if (a.axis == Dashboard::Plot::AxisKind::X) {
                            a.min = static_cast<float>(rect.X.Min);
                            a.max = static_cast<float>(rect.X.Max);
                        } else {
                            a.min = static_cast<float>(rect.Y.Min);
                            a.max = static_cast<float>(rect.Y.Max);
                        }
                    }
                }

                // handle hover detection if in layout mode
                if (mode == Mode::Layout) {
                    bool plotItemHovered = ImPlot::IsPlotHovered() || ImPlot::IsAxisHovered(ImAxis_X1) || ImPlot::IsAxisHovered(ImAxis_X2) || ImPlot::IsAxisHovered(ImAxis_X3) || ImPlot::IsAxisHovered(ImAxis_Y1) || ImPlot::IsAxisHovered(ImAxis_Y2) || ImPlot::IsAxisHovered(ImAxis_Y3);
                    if (!plotItemHovered) {
                        // Unfortunaetly there is no function that returns whether the entire legend is hovered,
                        // we need to check one entry at a time
                        for (const auto& source : plot.sources) {
                            const auto& sourceName = source->name();
                            if (ImPlot::IsLegendEntryHovered(sourceName.c_str())) {
                                plotItemHovered = true;

                                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                                    // TODO(NOW) which node was clicked -- it needs to have a sane way to get this?
                                    m_editPane.block     = nullptr; // dashboard.localFlowGraph.findBlock(source->blockName);
                                    m_editPane.closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay;
                                }
                                break;
                            }
                        }
                    }
                }

                TouchHandler<>::EndZoomablePlot();
                ImPlot::PopStyleVar(3);
            }
        };
    }

    m_dockSpace.render(windows, pane_size);

    if (toDelete) {
        dashboard.deletePlot(toDelete);
    }
}

void DashboardPage::drawGrid(float w, float h) {
    const uint32_t gridLineColor = LookAndFeel::instance().style == LookAndFeel::Style::Light ? 0x40000000 : 0x40ffffff;

    auto pos = ImGui::GetCursorScreenPos();
    for (float x = pos.x; x < pos.x + pane_size.x; x += w) {
        ImGui::GetWindowDrawList()->AddLine({x, pos.y}, {x, pos.y + pane_size.y}, gridLineColor);
    }
    for (float y = pos.y; y < pos.y + pane_size.y; y += h) {
        ImGui::GetWindowDrawList()->AddLine({pos.x, y}, {pos.x + pane_size.x, y}, gridLineColor);
    }
}

void DashboardPage::drawLegend([[maybe_unused]] Dashboard& dashboard, [[maybe_unused]] const DashboardPage::Mode& mode) noexcept {
#ifdef TODO_PORT // TODO: revisit this!!!
    alignForWidth(std::max(10.f, legend_box.x), 0.5f);
    legend_box.x = 0.f;
    {
        IMW::Group group;

        const auto legend_item = [](const ImVec4& color, std::string_view text, bool enabled = true) -> bool {
            const ImVec2 cursorPos = ImGui::GetCursorScreenPos();

            // Draw colored rectangle
            const ImVec4 modifiedColor = enabled ? color : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            const ImVec2 rectSize(ImGui::GetTextLineHeight() - 4, ImGui::GetTextLineHeight());
            ImGui::GetWindowDrawList()->AddRectFilled(cursorPos + ImVec2(0, 2), cursorPos + rectSize - ImVec2(0, 2), ImGui::ColorConvertFloat4ToU32(modifiedColor));
            bool pressed = ImGui::InvisibleButton("##Button", rectSize);
            ImGui::SameLine();

            // Draw button text with transparent background
            ImVec2          buttonSize(rectSize.x + ImGui::CalcTextSize(text.data()).x - 4, ImGui::GetTextLineHeight());
            IMW::StyleColor buttonStyle(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            IMW::StyleColor hoveredStyle(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.1f));
            IMW::StyleColor activeStyle(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.2f));
            IMW::StyleColor textStyle(ImGuiCol_Text, enabled ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            pressed |= ImGui::Button(text.data(), buttonSize);

            return pressed;
        };

        for (plf::colony<Dashboard::Source>::iterator iter = dashboard.sources().begin(); iter != dashboard.sources().end(); ++iter) { // N.B. colony doesn't have a bracket operator TODO: evaluate dependency
            Dashboard::Source& signal = *iter;
            auto               color  = ImGui::ColorConvertU32ToFloat4(signal.color);
            if (legend_item(color, signal.name, signal.visible)) {
                m_editPane.block     = dashboard.localFlowGraph.findBlock(signal.blockName);
                m_editPane.closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay;
            }
            legend_box.x += ImGui::GetItemRectSize().x;

            if (auto dndSource = IMW::DragDropSource(ImGuiDragDropFlags_None)) {
                DndItem dnd = {nullptr, &signal};
                ImGui::SetDragDropPayload(dnd_type, &dnd, sizeof(dnd));
                legend_item(color, signal.name, signal.visible);
            }

            if (const auto nextSignal = std::next(iter, 1); nextSignal != dashboard.sources().cend()) {
                const auto widthEstimate = ImGui::CalcTextSize(nextSignal->name.c_str()).x + 20 /* icon width */;
                if ((legend_box.x + widthEstimate) < 0.9f * pane_size.x) {
                    ImGui::SameLine(); // keep item on the same line if compatible with overall pane width
                } else {
                    legend_box.x = 0.f; // start a new line
                }
            }
        }
    }
    legend_box.x = ImGui::GetItemRectSize().x;
    legend_box.y = std::max(5.f, ImGui::GetItemRectSize().y);

    if (auto dndTarget = IMW::DragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(dnd_type)) {
            auto* dnd = static_cast<DndItem*>(payload->Data);
            if (auto plot = dnd->plotSource) {
                plot->sources.erase(std::find(plot->sources.begin(), plot->sources.end(), dnd->source));
            }
        }
    }
    // end draw legend
#endif
}

void DashboardPage::newPlot(Dashboard& dashboard) {
    if (dashboard.plots().size() < kMaxPlots) {
        // Plot will get adjusted by the layout automatically
        return dashboard.newPlot(0, 0, 1, 1);
    }
}

void DashboardPage::setLayoutType(DockingLayoutType type) { m_dockSpace.setLayoutType(type); }

} // namespace DigitizerUi
