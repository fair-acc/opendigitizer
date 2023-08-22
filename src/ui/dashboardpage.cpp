#include "dashboardpage.h"

#include <fmt/format.h>
#include <implot.h>

#include "app.h"
#include "flowgraph.h"
#include "flowgraph/datasink.h"
#include "imguiutils.h"

#include "utils/TouchHandler.hpp"

namespace DigitizerUi {

namespace {

struct ActionParameters {
    bool            frameHovered;
    bool            hoveredInTitleArea;
    ImVec2          screenOrigin;
    ImVec2          plotPos;
    ImVec2          plotSize;
    GridArrangement arrangement;
};

void updateFinalPlotSize(Dashboard::Plot *plot, DashboardPage::Action action, float cellW, float cellH) {
    auto drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);

    int  dx   = std::round(drag.x / cellW);
    int  dy   = std::round(drag.y / cellH);
    switch (action) {
    case DashboardPage::Action::None: break;
    case DashboardPage::Action::Move:
        plot->rect.x += dx;
        plot->rect.y += dy;
        break;
    case DashboardPage::Action::ResizeLeft:
        plot->rect.x += dx;
        plot->rect.w -= dx;
        break;
    case DashboardPage::Action::ResizeRight:
        plot->rect.w += dx;
        break;
    case DashboardPage::Action::ResizeTop:
        plot->rect.y += dy;
        plot->rect.h -= dy;
        break;
    case DashboardPage::Action::ResizeBottom:
        plot->rect.h += dy;
        break;
    case DashboardPage::Action::ResizeTopLeft:
        plot->rect.y += dy;
        plot->rect.h -= dy;
        plot->rect.x += dx;
        plot->rect.w -= dx;
        break;
    case DashboardPage::Action::ResizeTopRight:
        plot->rect.y += dy;
        plot->rect.h -= dy;
        plot->rect.w += dx;
        break;
    case DashboardPage::Action::ResizeBottomLeft:
        plot->rect.h += dy;
        plot->rect.x += dx;
        plot->rect.w -= dx;
        break;
    case DashboardPage::Action::ResizeBottomRight:
        plot->rect.h += dy;
        plot->rect.w += dx;
        break;
    }
}

void updatePlotSize(DashboardPage::Action action, ImVec2 &plotPos, ImVec2 &plotSize) {
    auto drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
    switch (action) {
    case DashboardPage::Action::None: break;
    case DashboardPage::Action::Move:
        plotPos.x += drag.x;
        plotPos.y += drag.y;
        break;
    case DashboardPage::Action::ResizeLeft:
        plotPos.x += drag.x;
        plotSize.x -= drag.x;
        break;
    case DashboardPage::Action::ResizeRight:
        plotSize.x += drag.x;
        break;
    case DashboardPage::Action::ResizeTop:
        plotPos.y += drag.y;
        plotSize.y -= drag.y;
        break;
    case DashboardPage::Action::ResizeBottom:
        plotSize.y += drag.y;
        break;
    case DashboardPage::Action::ResizeTopLeft:
        plotPos.x += drag.x;
        plotSize.x -= drag.x;
        plotPos.y += drag.y;
        plotSize.y -= drag.y;
        break;
    case DashboardPage::Action::ResizeTopRight:
        plotPos.y += drag.y;
        plotSize.y -= drag.y;
        plotSize.x += drag.x;
        break;
    case DashboardPage::Action::ResizeBottomLeft:
        plotPos.x += drag.x;
        plotSize.x -= drag.x;
        plotSize.y += drag.y;
        break;
    case DashboardPage::Action::ResizeBottomRight:
        plotSize.x += drag.x;
        plotSize.y += drag.y;
        break;
    }
}

enum Edges {
    Left   = 1,
    Right  = 2,
    Top    = 4,
    Bottom = 8
};

static inline Edges GetHoveredEdges(const ActionParameters &parameters) noexcept {
    auto pos                = ImGui::GetMousePos() - parameters.screenOrigin;
    int  edges              = 0;

    bool include_horizontal = parameters.arrangement != GridArrangement::Vertical;
    bool include_vertical   = parameters.arrangement != GridArrangement::Horizontal;

    if (pos.x < parameters.plotPos.x + 10) {
        edges |= Left * include_horizontal;
    } else if (pos.x > parameters.plotPos.x + parameters.plotSize.x - 10) {
        edges |= Right * include_horizontal;
    }
    if (pos.y < parameters.plotPos.y + 10) {
        edges |= Top * include_vertical;
    } else if (pos.y > parameters.plotPos.y + parameters.plotSize.y - 10) {
        edges |= Bottom * include_vertical;
    }
    return Edges(edges);
}

DashboardPage::Action getAction(const ActionParameters &parameters) {
    DashboardPage::Action finalAction = DashboardPage::Action::None;
    if (ImGui::IsItemHovered() && parameters.hoveredInTitleArea) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            finalAction = DashboardPage::Action::Move;
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    if (parameters.frameHovered && parameters.hoveredInTitleArea) {
        auto                  edges  = GetHoveredEdges(parameters);

        ImGuiMouseCursor      cursor = ImGuiMouseCursor_Hand;
        DashboardPage::Action action = DashboardPage::Action::Move;
        switch (edges) {
        case Left:
            cursor = ImGuiMouseCursor_ResizeEW;
            action = DashboardPage::Action::ResizeLeft;
            break;
        case Right:
            cursor = ImGuiMouseCursor_ResizeEW;
            action = DashboardPage::Action::ResizeRight;
            break;
        case Top:
            cursor = ImGuiMouseCursor_ResizeNS;
            action = DashboardPage::Action::ResizeTop;
            break;
        case Bottom:
            cursor = ImGuiMouseCursor_ResizeNS;
            action = DashboardPage::Action::ResizeBottom;
            break;
        case Left | Top:
            cursor = ImGuiMouseCursor_ResizeNWSE;
            action = DashboardPage::Action::ResizeTopLeft;
            break;
        case Right | Bottom:
            cursor = ImGuiMouseCursor_ResizeNWSE;
            action = DashboardPage::Action::ResizeBottomRight;
            break;
        case Left | Bottom:
            cursor = ImGuiMouseCursor_ResizeNESW;
            action = DashboardPage::Action::ResizeBottomLeft;
            break;
        case Right | Top:
            cursor = ImGuiMouseCursor_ResizeNESW;
            action = DashboardPage::Action::ResizeTopRight;
            break;
        default:
            break;
        }
        ImGui::SetMouseCursor(cursor);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            finalAction = action;
        }
    }

    if (finalAction != DashboardPage::Action::None) {
        ImGui::SetMouseCursor([&]() {
            using enum DashboardPage::Action;
            switch (finalAction) {
            case None: break;
            case Move: return ImGuiMouseCursor_Hand;
            case ResizeLeft:
            case ResizeRight: return ImGuiMouseCursor_ResizeEW;
            case ResizeTop:
            case ResizeBottom: return ImGuiMouseCursor_ResizeNS;
            case ResizeTopLeft:
            case ResizeBottomRight: return ImGuiMouseCursor_ResizeNWSE;
            case ResizeTopRight:
            case ResizeBottomLeft: return ImGuiMouseCursor_ResizeNESW;
            }
            return ImGuiMouseCursor_Arrow;
        }());
    }
    return finalAction;
}
} // namespace

static bool plotButton(App *app, const char *glyph, const char *tooltip) noexcept {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0.2f));
    ImGui::PushFont(app->fontIconsSolid);
    const bool ret = ImGui::Button(glyph);
    ImGui::PopFont();
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return ret;
}

static void alignForWidth(float width, float alignment = 0.5f) noexcept {
    ImGuiStyle &style = ImGui::GetStyle();
    float       avail = ImGui::GetContentRegionAvail().x;
    float       off   = (avail - width) * alignment;
    if (off > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
}

static void SetupAxes(const Dashboard::Plot &plot) {
    for (const auto &a : plot.axes) {
        const bool is_horizontal = a.axis == Dashboard::Plot::Axis::X;
        const auto axis          = is_horizontal ? ImAxis_X1 : ImAxis_Y1; // TODO: extend for multiple axis support (-> see ImPlot demo)
        // TODO: setup system where the label (essentially units) are derived from the signal units,
        // e.g. right-aligned '[utc]', 'time since first injection [ms]', `[Hz]`, '[A]', '[A]', '[V]', '[ppp]', '[GeV]', ...
        const auto      axisLabel = is_horizontal ? fmt::format("x-axis [a.u.]") : fmt::format("y-axis [a.u.]");

        ImPlotAxisFlags axisFlags = ImPlotAxisFlags_None;
        axisFlags |= is_horizontal ? (ImPlotAxisFlags_LockMin) : (ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);

        const auto estTextSize = ImGui::CalcTextSize(axisLabel.c_str()).x;
        if (estTextSize >= a.width) {
            static const auto estDotSize = ImGui::CalcTextSize("...").x;
            const std::size_t newSize    = a.width / std::max(1.f, estTextSize) * axisLabel.length();
            ImPlot::SetupAxis(axis, fmt::format("...{}", axisLabel.substr(axisLabel.length() - newSize, axisLabel.length())).c_str(), axisFlags);
        } else {
            ImPlot::SetupAxis(axis, axisLabel.c_str(), axisFlags);
        }
        if (is_horizontal && a.min < a.max) {
            ImPlot::SetupAxisLimits(axis, a.min, a.max);
        }
    }
}

void DashboardPage::drawPlot(DigitizerUi::Dashboard::Plot &plot) noexcept {
    SetupAxes(plot);
    ImPlot::SetupFinish();
    const auto axisSize = ImPlot::GetPlotSize();

    // compute axis pixel width for H and height for V
    [&plot] {
        const ImPlotRect axisLimits = ImPlot::GetPlotLimits(IMPLOT_AUTO, IMPLOT_AUTO);
        const auto       p0         = ImPlot::PlotToPixels(axisLimits.X.Min, axisLimits.Y.Min);
        const auto       p1         = ImPlot::PlotToPixels(axisLimits.X.Max, axisLimits.Y.Max);
        const int        xWidth     = static_cast<int>(std::abs(p1.x - p0.x));
        const int        yHeight    = static_cast<int>(std::abs(p1.y - p0.y));
        std::for_each(plot.axes.begin(), plot.axes.end(), [xWidth, yHeight](auto &a) { a.width = (a.axis == Dashboard::Plot::Axis::X) ? xWidth : yHeight; });
    }();

    for (auto *source : plot.sources) {
        auto color = ImGui::ColorConvertU32ToFloat4(source->color);
        ImPlot::SetNextLineStyle(color);

        auto *sink = static_cast<DataSink *>(source->block);
        ImPlot::HideNextItem(false, ImPlotCond_Always);
        sink->update();
        if (sink->data.empty()) {
            // Plot one single dummy value so that the sink shows up in the plot legend
            float v = 0;
            if (source->visible) {
                ImPlot::PlotLine(source->name.c_str(), &v, 1);
            }
        } else if (source->visible) {
            switch (sink->dataType) {
            case DigitizerUi::DataType::Float32: {
                auto            values = sink->data.asFloat32();
                std::lock_guard lock(sink->m_mutex);
                ImPlot::PlotLine(source->name.c_str(), values.data(), int(values.size()));
                break;
            }
            case DigitizerUi::DataType::DataSetFloat32: {
                auto            ds = sink->data.asDataSetFloat32();
                if (ds.extents.empty()) {
                    break;
                }
                auto &values = ds.signal_values;
                std::lock_guard lock(sink->m_mutex);
                for (int i = 0; i < ds.extents[0]; ++i) {
                    auto n = ds.extents[1];
                    ImPlot::PlotLine(ds.signal_names[i].c_str(), values.data() + n*i, n);
                }
                break;
            }
            default: break;
            }
        }

        // allow legend item labels to be DND sources
        if (ImPlot::BeginDragDropSourceItem(source->name.c_str())) {
            DigitizerUi::DashboardPage::DndItem dnd = { &plot, source };
            ImGui::SetDragDropPayload(dnd_type, &dnd, sizeof(dnd));
            ImPlot::ItemIcon(color);
            ImGui::SameLine();
            ImGui::TextUnformatted(source->name.c_str());
            ImPlot::EndDragDropSource();
        }
    }
}

void DashboardPage::draw(App *app, Dashboard *dashboard, Mode mode) noexcept {
    const float     left              = ImGui::GetCursorPosX();
    const float     top               = ImGui::GetCursorPosY();
    const ImVec2    size              = ImGui::GetContentRegionAvail();

    const bool      horizontalSplit   = size.x > size.y;
    constexpr float splitterWidth     = 6;
    constexpr float halfSplitterWidth = splitterWidth / 2.f;
    const float     ratio             = ImGuiUtils::splitter(size, horizontalSplit, splitterWidth, 0.2f, !m_editPane.block);

    ImGui::SetCursorPosX(left);
    ImGui::SetCursorPosY(top);

    ImGui::BeginChild("##plots", horizontalSplit ? ImVec2(size.x * (1.f - ratio) - halfSplitterWidth, size.y) : ImVec2(size.x, size.y * (1.f - ratio) - halfSplitterWidth),
            false, ImGuiWindowFlags_NoScrollbar);

    if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_editPane.block = nullptr;
    }

    // Plots
    ImGui::BeginGroup();
    drawPlots(app, mode, dashboard);
    ImGui::EndGroup(); // Plots

    ImGui::SetCursorPos(ImVec2(0, ImGui::GetWindowHeight() - legend_box.y));

    // Legend
    ImGui::BeginGroup();
    // Button strip
    if (mode == Mode::Layout) {
        if (plotButton(app, "\uF201", "create new chart")) // chart-line
            newPlot(dashboard);
        ImGui::SameLine();
        if (plotButton(app, "\uF7A5", "change to the horizontal layout"))
            plot_layout.SetArrangement(GridArrangement::Horizontal);
        ImGui::SameLine();
        if (plotButton(app, "\uF7A4", "change to the vertical layout"))
            plot_layout.SetArrangement(GridArrangement::Vertical);
        ImGui::SameLine();
        if (plotButton(app, "\uF58D", "change to the grid layout"))
            plot_layout.SetArrangement(GridArrangement::Tiles);
        ImGui::SameLine();
        if (plotButton(app, "\uF248", "change to the free layout"))
            plot_layout.SetArrangement(GridArrangement::Free);
        ImGui::SameLine();
    }

    drawLegend(app, dashboard, mode);

    // Post button strip
    if (mode == Mode::Layout) {
        ImGui::SameLine();
        if (plotButton(app, "\uf067", "add signal")) { // plus
            // add new signal
        }
    }

    if (app->prototypeMode) {
        ImGui::SameLine();
        // Retrieve FPS and milliseconds per iteration
        const float fps     = ImGui::GetIO().Framerate;
        const auto  str     = fmt::format("FPS:{:5.0f}({:2}ms)", fps, app->execTime.count());
        const auto  estSize = ImGui::CalcTextSize(str.c_str());
        alignForWidth(estSize.x, 1.0);
        ImGui::Text("%s", str.c_str());
    }
    ImGui::EndGroup(); // Legend
    legend_box.y = std::floor(ImGui::GetItemRectSize().y * 1.5f);

    ImGui::EndChild();

    if (horizontalSplit) {
        const float w = size.x * ratio;
        ImGuiUtils::drawBlockControlsPanel(m_editPane, { left + size.x - w + halfSplitterWidth, top }, { w - halfSplitterWidth, size.y }, true);
    } else {
        const float h = size.y * ratio;
        ImGuiUtils::drawBlockControlsPanel(m_editPane, { left, top + size.y - h + halfSplitterWidth }, { size.x, h - halfSplitterWidth }, false);
    }
}

void DashboardPage::drawPlots(App *app, DigitizerUi::DashboardPage::Mode mode, Dashboard *dashboard) {
    pane_size = ImGui::GetContentRegionAvail();
    pane_size.y -= legend_box.y;

    float w = pane_size.x / float(GridLayout::grid_width);  // Grid width
    float h = pane_size.y / float(GridLayout::grid_height); // Grid height

    if (mode == Mode::Layout)
        drawGrid(w, h);

    auto pos       = ImGui::GetCursorPos();
    auto screenPos = ImGui::GetCursorScreenPos();

    if (mode == Mode::Layout && clicked_plot && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        updateFinalPlotSize(clicked_plot, clicked_action, w, h);
        clicked_plot   = nullptr;
        clicked_action = DashboardPage::Action::None;
    }

    Dashboard::Plot *toDelete = nullptr;

    // with the dark style the plot frame would have the same color as a button. make it have the
    // same color as the window background instead.
    ImPlot::GetStyle().Colors[ImPlotCol_FrameBg] = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];

    plot_layout.ArrangePlots(dashboard->plots());

    for (auto &plot : dashboard->plots()) {
        const float offset       = mode == Mode::Layout ? 5 : 0;

        auto        plotPos      = ImVec2(w * plot.rect.x, h * plot.rect.y);
        auto        plotSize     = ImVec2{ plot.rect.w * w, plot.rect.h * h };

        bool        frameHovered = [&]() {
            if (mode != Mode::Layout)
                return false;

            if (clicked_plot == &plot && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                updatePlotSize(clicked_action, plotPos, plotSize);
            }

            ImGui::SetCursorPos(pos + plotPos);
            ImGui::InvisibleButton("##ss", plotSize);
            ImGui::SetItemAllowOverlap(); // this is needed to make the remove button work

            bool           frameHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

            ImVec2         p1           = screenPos + plotPos;
            ImVec2         p2           = p1 + plotSize;
            const uint32_t color        = ImGui::GetColorU32(frameHovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
            ImGui::GetWindowDrawList()->AddRectFilled(p1, p2, color);

            return frameHovered;
        }();

        ImGui::SetCursorPos(pos + plotPos + ImVec2(offset, offset));
        const bool  showTitle = false; // TODO: make this and the title itself a configurable/editable entity
        ImPlotFlags plotFlags = ImPlotFlags_NoChild;
        plotFlags |= showTitle ? ImPlotFlags_None : ImPlotFlags_NoTitle;
        plotFlags |= mode == Mode::Layout ? ImPlotFlags_None : ImPlotFlags_NoLegend;

        ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2{ 0, 0 }); // TODO: make this perhaps a global style setting via ImPlot::GetStyle()
        ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding, ImVec2{ 3, 1 });
        if (fair::TouchHandler<>::BeginZoomablePlot(plot.name, plotSize - ImVec2(2 * offset, 2 * offset), plotFlags)) {
            drawPlot(plot);

            // allow the main plot area to be a DND target
            if (ImPlot::BeginDragDropTargetPlot()) {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(dnd_type)) {
                    auto *dnd = static_cast<DndItem *>(payload->Data);
                    plot.sources.push_back(dnd->source);
                    if (auto *plot = dnd->plotSource) {
                        plot->sources.erase(std::find(plot->sources.begin(), plot->sources.end(), dnd->source));
                    }
                }
                ImPlot::EndDragDropTarget();
            }

            auto rect = ImPlot::GetPlotLimits();
            for (auto &a : plot.axes) {
                if (a.axis == Dashboard::Plot::Axis::X) {
                    a.min = rect.X.Min;
                    a.max = rect.X.Max;
                } else {
                    a.min = rect.Y.Min;
                    a.max = rect.Y.Max;
                }
            }

            bool plotItemHovered = false;
            if (mode == Mode::Layout) {
                plotItemHovered = ImPlot::IsPlotHovered() || ImPlot::IsAxisHovered(ImAxis_X1) || ImPlot::IsAxisHovered(ImAxis_X2) || ImPlot::IsAxisHovered(ImAxis_X3) || ImPlot::IsAxisHovered(ImAxis_Y1) || ImPlot::IsAxisHovered(ImAxis_Y2) || ImPlot::IsAxisHovered(ImAxis_Y3);
                if (!plotItemHovered) {
                    // Unfortunaetly there is no function that returns whether the entire legend is hovered,
                    // we need to check one entry at a time
                    for (const auto &s : plot.sources) {
                        if (ImPlot::IsLegendEntryHovered(s->name.c_str())) {
                            plotItemHovered = true;

                            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                                m_editPane.block     = s->block;
                                m_editPane.closeTime = std::chrono::system_clock::now() + App::instance().editPaneCloseDelay;
                            }

                            break;
                        }
                    }
                }
            }

            fair::TouchHandler<>::EndZoomablePlot();
            ImPlot::PopStyleVar(2);

            if (mode == Mode::Layout) {
                if (frameHovered) {
                    ImGui::PushFont(app->fontIcons);
                    ImGui::SetCursorPos(pos + plotPos + ImVec2(plotSize.x, 0) - ImVec2(30, -15)); // TODO: magic numbers
                    ImGui::PushID(plot.name.c_str());
                    if (ImGui::Button("\uf2ed")) {
                        toDelete = &plot;
                    }
                    ImGui::PopID();
                    ImGui::PopFont();
                }

                auto action = getAction({ frameHovered, !plotItemHovered, screenPos, plotPos, plotSize, plot_layout.Arrangement() });
                if (action != DashboardPage::Action::None) {
                    clicked_action = action;
                    clicked_plot   = &plot;
                }
            }
        } // if (ImPlot::BeginPlot() {...} TODO: method/branch is too long
    }
    if (toDelete) {
        dashboard->deletePlot(toDelete);
    }
}

void DashboardPage::drawGrid(float w, float h) {
    const uint32_t gridLineColor = App::instance().style() == Style::Light ? 0x40000000 : 0x40ffffff;

    auto           pos           = ImGui::GetCursorScreenPos();
    for (float x = pos.x; x < pos.x + pane_size.x; x += w) {
        ImGui::GetWindowDrawList()->AddLine({ x, pos.y }, { x, pos.y + pane_size.y }, gridLineColor);
    }
    for (float y = pos.y; y < pos.y + pane_size.y; y += h) {
        ImGui::GetWindowDrawList()->AddLine({ pos.x, y }, { pos.x + pane_size.x, y }, gridLineColor);
    }
}

void DashboardPage::drawLegend(App *app, Dashboard *dashboard, const DashboardPage::Mode &mode) noexcept {
    alignForWidth(std::max(10.f, legend_box.x), 0.5f);
    legend_box.x = 0.f;
    ImGui::BeginGroup();

    const auto legend_item = [](const ImVec4 &color, std::string_view text, bool enabled = true) -> bool {
        const ImVec2 cursorPos = ImGui::GetCursorScreenPos();

        // Draw colored rectangle
        const ImVec4 modifiedColor = enabled ? color : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        const ImVec2 rectSize(ImGui::GetTextLineHeight() - 4, ImGui::GetTextLineHeight());
        ImGui::GetWindowDrawList()->AddRectFilled(cursorPos + ImVec2(0, 2), cursorPos + rectSize - ImVec2(0, 2), ImGui::ColorConvertFloat4ToU32(modifiedColor));
        bool pressed = ImGui::InvisibleButton("##Button", rectSize);
        ImGui::SameLine();

        // Draw button text with transparent background
        ImVec2 buttonSize(rectSize.x + ImGui::CalcTextSize(text.data()).x - 4, ImGui::GetTextLineHeight());
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.1f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.2f));
        ImGui::PushStyleColor(ImGuiCol_Text, enabled ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        pressed |= ImGui::Button(text.data(), buttonSize);
        ImGui::PopStyleColor(4);
        return pressed;
    };

    for (plf::colony<Dashboard::Source>::iterator iter = dashboard->sources().begin(); iter != dashboard->sources().end(); ++iter) { // N.B. colony doesn't have a bracket operator TODO: evaluate dependency
        Dashboard::Source &signal = *iter;
        auto               color  = ImGui::ColorConvertU32ToFloat4(signal.color);
        if (legend_item(color, signal.name, signal.visible)) {
            fmt::print("click\n");
            m_editPane.block     = signal.block;
            m_editPane.closeTime = std::chrono::system_clock::now() + App::instance().editPaneCloseDelay;
        }
        legend_box.x += ImGui::GetItemRectSize().x;

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            DndItem dnd = { nullptr, &signal };
            ImGui::SetDragDropPayload(dnd_type, &dnd, sizeof(dnd));
            legend_item(color, signal.name, signal.visible);
            ImGui::EndDragDropSource();
        }

        if (const auto nextSignal = std::next(iter, 1); nextSignal != dashboard->sources().cend()) {
            const auto widthEstimate = ImGui::CalcTextSize(nextSignal->name.c_str()).x + 20 /* icon width */;
            if ((legend_box.x + widthEstimate) < 0.9f * pane_size.x) {
                ImGui::SameLine(); // keep item on the same line if compatible with overall pane width
            } else {
                legend_box.x = 0.f; // start a new line
            }
        }
    }
    ImGui::EndGroup();
    legend_box.x = ImGui::GetItemRectSize().x;
    legend_box.y = std::max(5.f, ImGui::GetItemRectSize().y);

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(dnd_type)) {
            auto *dnd = static_cast<DndItem *>(payload->Data);
            if (auto plot = dnd->plotSource) {
                plot->sources.erase(std::find(plot->sources.begin(), plot->sources.end(), dnd->source));
            }
        }
        ImGui::EndDragDropTarget();
    }
    // end draw legend
}

void DashboardPage::newPlot(Dashboard *dashboard) {
    if (plot_layout.Arrangement() != GridArrangement::Free
            && dashboard->plots().size() < GridLayout::max_plots) {
        plot_layout.Invalidate();
        return dashboard->newPlot(0, 0, 1, 1); // Plot will get adjusted by the layout automatically
    }

    bool grid[GridLayout::grid_width][GridLayout::grid_height];
    memset(grid, 0, sizeof(grid));

    for (auto &p : dashboard->plots()) {
        for (int x = p.rect.x; x < p.rect.x + p.rect.w; ++x) {
            for (int y = p.rect.y; y < p.rect.y + p.rect.h; ++y) {
                grid[x][y] = true;
            }
        }
    }

    // This algorithm to find a free spot in the grid is really dumb and not optimized,
    // however the grid is so small and it runs so unfrequently that it doesn't really matter.
    auto rectangleFree = [&](int x, int y, int w, int h) {
        for (int y2 = y; y2 < y + h; ++y2) {
            for (int x2 = x; x2 < x + w; ++x2) {
                if (x2 == GridLayout::grid_width || y2 == GridLayout::grid_height || grid[x2][y2]) {
                    return false;
                }
            }
        }
        return true;
    };

    auto findRectangle = [&](int &rx, int &ry, int &rw, int &rh) {
        int i = 0;
        while (true) {
            for (int y = 0; y < GridLayout::grid_height; ++y) {
                for (int x = 0; x < GridLayout::grid_width; ++x) {
                    if (rectangleFree(x, y, rw, rh)) {
                        rx = x;
                        ry = y;
                        return true;
                    }
                }
            }
            if (rw == 1 || rh == 1) {
                break;
            }

            if (i % 2 == 0) {
                rw -= 1;
            } else {
                rh -= 1;
            }
            ++i;
        }
        return false;
    };

    int w = GridLayout::grid_width / 2;
    int h = w * 6. / 8.;

    int x, y;
    if (findRectangle(x, y, w, h)) {
        dashboard->newPlot(x, y, w, h);
    }
}

} // namespace DigitizerUi
