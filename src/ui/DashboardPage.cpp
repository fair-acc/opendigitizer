#include "DashboardPage.hpp"

#include <fmt/format.h>
#include <implot.h>

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"
#include "common/TouchHandler.hpp"

#include "components/Splitter.hpp"

#include "Flowgraph.hpp"

namespace DigitizerUi {

namespace {

constexpr static inline auto kMaxPlots   = 16u;
constexpr static inline auto kGridWidth  = 16u;
constexpr static inline auto kGridHeight = 16u;

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
    ImGuiStyle& style = ImGui::GetStyle();
    float       avail = ImGui::GetContentRegionAvail().x;
    float       off   = (avail - width) * alignment;
    if (off > 0.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
    }
}

inline void SetupAxes(const Dashboard::Plot& plot) {
    using Axis = Dashboard::Plot::Axis;
    for (const auto& a : plot.axes) {
        // Determine if this is X or Y
        const bool   isHorizontal = (a.axis == Axis::X);
        const ImAxis axisId       = isHorizontal ? ImAxis_X1 : ImAxis_Y1;
        std::string  axisLabel    = isHorizontal ? "x-axis [a.u.]" : "y-axis [a.u.]";

        ImPlotAxisFlags axisFlags   = ImPlotAxisFlags_None;
        const bool      isFiniteMin = std::isfinite(a.min);
        const bool      isFiniteMax = std::isfinite(a.max);
        if (isFiniteMin && !isFiniteMax) {
            // keep min locked, auto-fit max
            axisFlags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
            axisFlags |= ImPlotAxisFlags_LockMin;
        } else if (!isFiniteMin && isFiniteMax) {
            // keep max locked, auto-fit min
            axisFlags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
            axisFlags |= ImPlotAxisFlags_LockMax;
        } else if (!isFiniteMin && !isFiniteMax) {
            // auto-fit min and max
            axisFlags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
        }

        // Truncate label if there isn’t enough space
        const float estTextWidth = ImGui::CalcTextSize(axisLabel.c_str()).x;
        if (estTextWidth >= a.width) {
            static const float ellipsisWidth = ImGui::CalcTextSize("...").x;
            if (a.width > ellipsisWidth + 1.0f) {
                // compute how many characters fit
                const float scaleFactor   = (a.width - ellipsisWidth) / std::max(1.0f, estTextWidth);
                const auto  fitCharCount  = static_cast<std::size_t>(std::floor(scaleFactor * axisLabel.size()));
                const auto  truncatedPart = axisLabel.substr(axisLabel.size() - fitCharCount);
                ImPlot::SetupAxis(axisId, fmt::format("...{}", truncatedPart).c_str(), axisFlags);
            } else {
                ImPlot::SetupAxis(axisId, "...", axisFlags);
            }
        } else {
            ImPlot::SetupAxis(axisId, axisLabel.c_str(), axisFlags);
        }

        if (isFiniteMin && isFiniteMax) { // set axis limits explicitly if both bounds are valid
            ImPlot::SetupAxisLimits(axisId, std::min(a.min, a.max), std::max(a.min, a.max));
        }
    }
}

void DashboardPage::drawPlot(Dashboard& dashboard, DigitizerUi::Dashboard::Plot& plot) noexcept {
    SetupAxes(plot);
    ImPlot::SetupFinish();
    const auto axisSize = ImPlot::GetPlotSize();

    // compute axis pixel width for H and height for V
    [&plot] {
        const ImPlotRect axisLimits = ImPlot::GetPlotLimits(IMPLOT_AUTO, IMPLOT_AUTO);
        const ImVec2     p0         = ImPlot::PlotToPixels(axisLimits.X.Min, axisLimits.Y.Min);
        const ImVec2     p1         = ImPlot::PlotToPixels(axisLimits.X.Max, axisLimits.Y.Max);
        const float      xWidth     = std::round(std::abs(p1.x - p0.x));
        const float      yHeight    = std::round(std::abs(p1.y - p0.y));
        std::for_each(plot.axes.begin(), plot.axes.end(), [xWidth, yHeight](auto& a) { a.width = (a.axis == Dashboard::Plot::Axis::X) ? xWidth : yHeight; });
    }();

    bool drawTag = true;
    for (const auto& source : plot.sources) {
        auto grBlock = dashboard.localFlowGraph.findPlotSinkGrBlock(source->name);
        if (!grBlock) {
            continue;
        }

        if (source->visible) {
            grBlock->draw({{"draw_tag", drawTag}} /* pass plot specific rendering options, axes, ...*/);
            drawTag = false;
        } else {
            // Consume data to not block the flowgraph
            std::ignore = grBlock->work(std::numeric_limits<std::size_t>::max());
        }

        // allow legend item labels to be DND sources
        if (ImPlot::BeginDragDropSourceItem(source->name.c_str())) {
            DigitizerUi::DashboardPage::DndItem dnd = {&plot, source};
            ImGui::SetDragDropPayload(dnd_type, &dnd, sizeof(dnd));
            const auto color = [&grBlock] {
                static const auto defaultColor = ImVec4(1, 0, 0, 1);
                const auto        maybeColor   = grBlock->settings().get("color");
                if (!maybeColor) {
                    return defaultColor;
                }
                const auto colorValue = maybeColor.value();
                const auto cv         = std::get_if<std::vector<float>>(&colorValue);
                if (!cv || cv->size() != 4) {
                    return defaultColor;
                }
                return ImVec4((*cv)[0], (*cv)[1], (*cv)[2], (*cv)[3]);
            }();

            ImPlot::ItemIcon(color);
            ImGui::SameLine();
            ImGui::TextUnformatted(source->name.c_str());
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
                if (plotButton("\uF201", "create new chart")) { // chart-line
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
                if (plotButton("\uf067", "add signal") && m_signalSelector) { // plus
                    // add new signal
                    m_signalSelector->open();
                }
            }

            if (m_signalSelector) {
                m_signalSelector->setAddSignalCallback([&](Block* block) { addSignalCallback(dashboard, block); });
                m_signalSelector->draw(&dashboard.localFlowGraph);
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
        components::BlockControlsPanel(dashboard, *this, m_editPane, {left + size.x - w + halfSplitterWidth, top}, {w - halfSplitterWidth, size.y}, true);
    } else {
        const float h = size.y * ratio;
        components::BlockControlsPanel(dashboard, *this, m_editPane, {left, top + size.y - h + halfSplitterWidth}, {size.x, h - halfSplitterWidth}, false);
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
        plot.window->renderFunc = [this, &dashboard, &plot, &toDelete, mode] {
            const float offset = mode == Mode::Layout ? 5 : 0;

            const bool  showTitle = false; // TODO: make this and the title itself a configurable/editable entity
            ImPlotFlags plotFlags = ImPlotFlags_NoChild;
            plotFlags |= showTitle ? ImPlotFlags_None : ImPlotFlags_NoTitle;
            plotFlags |= mode == Mode::Layout ? ImPlotFlags_None : ImPlotFlags_NoLegend;

            ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2{0, 0}); // TODO: make this perhaps a global style setting via ImPlot::GetStyle()
            ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding, ImVec2{3, 1});
            auto plotSize = ImGui::GetContentRegionAvail();
            if (TouchHandler<>::BeginZoomablePlot(plot.name, plotSize - ImVec2(2 * offset, 2 * offset), plotFlags)) {
                drawPlot(dashboard, plot);

                // allow the main plot area to be a DND target
                if (ImPlot::BeginDragDropTargetPlot()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(dnd_type)) {
                        auto* dnd = static_cast<DndItem*>(payload->Data);
                        plot.sources.push_back(dnd->source);
                        if (auto* plot = dnd->plotSource) {
                            plot->sources.erase(std::find(plot->sources.begin(), plot->sources.end(), dnd->source));
                        }
                    }
                    ImPlot::EndDragDropTarget();
                }

                ImPlotRect rect = ImPlot::GetPlotLimits();
                for (auto& a : plot.axes) {
                    const ImAxis    axisId           = (a.axis == Dashboard::Plot::Axis::X) ? ImAxis_X1 : ImAxis_Y1; // TODO: extend for multi-axes
                    ImPlotAxisFlags axisFlags        = ImPlot::GetCurrentPlot()->Axes[axisId].Flags;
                    const bool      isAutoOrRangeFit = (axisFlags & (ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit)) != 0;

                    if (!isAutoOrRangeFit) { // update a.min/a.max if the axis is *not* auto-fitting
                        if (a.axis == Dashboard::Plot::Axis::X) {
                            a.min = static_cast<float>(rect.X.Min);
                            a.max = static_cast<float>(rect.X.Max);
                        } else {
                            a.min = static_cast<float>(rect.Y.Min);
                            a.max = static_cast<float>(rect.Y.Max);
                        }
                    }
                }

                if (mode == Mode::Layout) {
                    bool plotItemHovered = ImPlot::IsPlotHovered() || ImPlot::IsAxisHovered(ImAxis_X1) || ImPlot::IsAxisHovered(ImAxis_X2) || ImPlot::IsAxisHovered(ImAxis_X3) || ImPlot::IsAxisHovered(ImAxis_Y1) || ImPlot::IsAxisHovered(ImAxis_Y2) || ImPlot::IsAxisHovered(ImAxis_Y3);
                    if (!plotItemHovered) {
                        // Unfortunaetly there is no function that returns whether the entire legend is hovered,
                        // we need to check one entry at a time
                        for (const auto& s : plot.sources) {
                            if (ImPlot::IsLegendEntryHovered(s->name.c_str())) {
                                plotItemHovered = true;

                                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                                    // TODO(NOW) which node was clicked -- it needs to have a sane way to get this?
                                    m_editPane.block     = nullptr; // dashboard.localFlowGraph.findBlock(s->blockName);
                                    m_editPane.closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay;
                                }

                                break;
                            }
                        }
                    }
                }

                TouchHandler<>::EndZoomablePlot();
                ImPlot::PopStyleVar(2);
            } // if (ImPlot::BeginPlot() {...} TODO: method/branch is too long
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
                fmt::print("click\n");
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

void DashboardPage::addSignalCallback(Dashboard& dashboard, Block* block) {
    ImGui::CloseCurrentPopup();
    auto* newsink = dashboard.createSink();
    dashboard.localFlowGraph.connect(&block->outputs()[0], &newsink->inputs()[0]);
    newPlot(dashboard);
    auto source = std::ranges::find_if(dashboard.sources(), [newsink](const auto& s) { return s.blockName == newsink->name; });
    dashboard.plots().back().sourceNames.push_back(source->name);
}

void DashboardPage::setLayoutType(DockingLayoutType type) { m_dockSpace.setLayoutType(type); }

} // namespace DigitizerUi
