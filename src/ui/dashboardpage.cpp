#include "dashboardpage.h"

#include <fmt/format.h>
#include <implot.h>

#include "app.h"
#include "flowgraph.h"
#include "flowgraph/datasink.h"
#include "imguiutils.h"

namespace DigitizerUi {

namespace {

enum Action {
    None,
    Move,
    ResizeLeft,
    ResizeRight,
    ResizeTop,
    ResizeBottom,
    ResizeTopLeft,
    ResizeTopRight,
    ResizeBottomLeft,
    ResizeBottomRight
};

void updateFinalPlotSize(Dashboard::Plot *plot, Action action, float cellW, float cellH) {
    auto drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);

    int  dx   = std::round(drag.x / cellW);
    int  dy   = std::round(drag.y / cellH);
    switch (action) {
    case Action::None: break;
    case Action::Move:
        plot->rect.x += dx;
        plot->rect.y += dy;
        break;
    case Action::ResizeLeft:
        plot->rect.x += dx;
        plot->rect.w -= dx;
        break;
    case Action::ResizeRight:
        plot->rect.w += dx;
        break;
    case Action::ResizeTop:
        plot->rect.y += dy;
        plot->rect.h -= dy;
        break;
    case Action::ResizeBottom:
        plot->rect.h += dy;
        break;
    case Action::ResizeTopLeft:
        plot->rect.y += dy;
        plot->rect.h -= dy;
        plot->rect.x += dx;
        plot->rect.w -= dx;
        break;
    case Action::ResizeTopRight:
        plot->rect.y += dy;
        plot->rect.h -= dy;
        plot->rect.w += dx;
        break;
    case Action::ResizeBottomLeft:
        plot->rect.h += dy;
        plot->rect.x += dx;
        plot->rect.w -= dx;
        break;
    case Action::ResizeBottomRight:
        plot->rect.h += dy;
        plot->rect.w += dx;
        break;
    }
}

void updatePlotSize(Action action, ImVec2 &plotPos, ImVec2 &plotSize) {
    auto drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
    switch (action) {
    case Action::None: break;
    case Action::Move:
        plotPos.x += drag.x;
        plotPos.y += drag.y;
        break;
    case Action::ResizeLeft:
        plotPos.x += drag.x;
        plotSize.x -= drag.x;
        break;
    case Action::ResizeRight:
        plotSize.x += drag.x;
        break;
    case Action::ResizeTop:
        plotPos.y += drag.y;
        plotSize.y -= drag.y;
        break;
    case Action::ResizeBottom:
        plotSize.y += drag.y;
        break;
    case Action::ResizeTopLeft:
        plotPos.x += drag.x;
        plotSize.x -= drag.x;
        plotPos.y += drag.y;
        plotSize.y -= drag.y;
        break;
    case Action::ResizeTopRight:
        plotPos.y += drag.y;
        plotSize.y -= drag.y;
        plotSize.x += drag.x;
        break;
    case Action::ResizeBottomLeft:
        plotPos.x += drag.x;
        plotSize.x -= drag.x;
        plotSize.y += drag.y;
        break;
    case Action::ResizeBottomRight:
        plotSize.x += drag.x;
        plotSize.y += drag.y;
        break;
    }
}

Action getAction(bool frameHovered, bool hoveredInTitleArea, const ImVec2 &screenOrigin,
        const ImVec2 &plotPos, const ImVec2 &plotSize) {
    Action finalAction = Action::None;
    if (ImGui::IsItemHovered() && hoveredInTitleArea) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            finalAction = Action::Move;
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    if (frameHovered && hoveredInTitleArea) {
        auto pos   = ImGui::GetMousePos() - screenOrigin;
        int  edges = 0;
        enum Edges { Left = 1,
            Right         = 2,
            Top           = 4,
            Bottom        = 8 };
        ImGuiMouseCursor cursor = ImGuiMouseCursor_Hand;
        if (pos.x < plotPos.x + 10) {
            edges |= Left;
        } else if (pos.x > plotPos.x + plotSize.x - 10) {
            edges |= Right;
        }
        if (pos.y < plotPos.y + 10) {
            edges |= Top;
        } else if (pos.y > plotPos.y + plotSize.y - 10) {
            edges |= Bottom;
        }
        Action action = Action::Move;
        switch (edges) {
        case Left:
            cursor = ImGuiMouseCursor_ResizeEW;
            action = Action::ResizeLeft;
            break;
        case Right:
            cursor = ImGuiMouseCursor_ResizeEW;
            action = Action::ResizeRight;
            break;
        case Top:
            cursor = ImGuiMouseCursor_ResizeNS;
            action = Action::ResizeTop;
            break;
        case Bottom:
            cursor = ImGuiMouseCursor_ResizeNS;
            action = Action::ResizeBottom;
            break;
        case Left | Top:
            cursor = ImGuiMouseCursor_ResizeNWSE;
            action = Action::ResizeTopLeft;
            break;
        case Right | Bottom:
            cursor = ImGuiMouseCursor_ResizeNWSE;
            action = Action::ResizeBottomRight;
            break;
        case Left | Bottom:
            cursor = ImGuiMouseCursor_ResizeNESW;
            action = Action::ResizeBottomLeft;
            break;
        case Right | Top:
            cursor = ImGuiMouseCursor_ResizeNESW;
            action = Action::ResizeTopRight;
            break;
        default:
            break;
        }
        ImGui::SetMouseCursor(cursor);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            finalAction = action;
        }
    }

    if (finalAction != Action::None) {
        ImGui::SetMouseCursor([&]() {
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



void DashboardPage::draw(App *app, Dashboard *dashboard, Mode mode) noexcept {
    drawPlots(app, mode, dashboard);

    // Button strip
    if (mode == Mode::Layout) {
        if (plotButton(app, "\uF201", "create new chart")) // chart-line
            newPlot(dashboard);
        ImGui::SameLine();
        if (plotButton(app, "\u25EB", "change to the horizontal layout"))
            plot_layout.SetArrangement(GridArrangement::Horizontal);
        ImGui::SameLine();
        if (plotButton(app, "\u229F", "change to the vertical layout"))
            plot_layout.SetArrangement(GridArrangement::Vertical);
        ImGui::SameLine();
        if (plotButton(app, "\u229E", "change to the grid layout"))
            plot_layout.SetArrangement(GridArrangement::Tiles);
        ImGui::SameLine();
        if (plotButton(app, "\u25F3", "change to the free layout"))
            plot_layout.SetArrangement(GridArrangement::Free);
    }

    drawLegend(app, dashboard, mode);

    ImGui::SameLine();
    if (mode == Mode::Layout && plotButton(app, "\uf067", "add signal")) { // plus
        // add new signal
    }

    if (app->prototypeMode) {
        // Retrieve FPS and milliseconds per iteration
        const float fps     = ImGui::GetIO().Framerate;
        const auto  str     = fmt::format("FPS:{:5.0f}({:2}ms)", fps, app->execTime.count());
        const auto  estSize = ImGui::CalcTextSize(str.c_str());
        alignForWidth(estSize.x, 1.0);
        ImGui::Text("%s", str.c_str());
    }
}



void DashboardPage::drawPlots(App *app, DigitizerUi::DashboardPage::Mode mode, Dashboard *dashboard) {
    struct Guard {
        Guard() noexcept { ImGui::BeginGroup(); }
        ~Guard() noexcept { ImGui::EndGroup(); }
    } g;

    pane_size = ImGui::GetContentRegionAvail();
    pane_size.y -= legend_box.y;

    float w = pane_size.x / float(GridLayout::grid_width); // Grid width
    float h = pane_size.y / float(GridLayout::grid_height); // Grid height

    if (mode == Mode::Layout)
        drawGrid(w, h);

    auto                    pos           = ImGui::GetCursorPos();
    auto                    screenPos     = ImGui::GetCursorScreenPos();

    static Dashboard::Plot *clickedPlot   = nullptr;
    static Action           clickedAction = Action::None;

    if (mode == Mode::Layout && clickedPlot && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        updateFinalPlotSize(clickedPlot, clickedAction, w, h);
        clickedPlot   = nullptr;
        clickedAction = Action::None;
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

            if (clickedPlot == &plot && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                updatePlotSize(clickedAction, plotPos, plotSize);
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
        if (ImPlot::BeginPlot(plot.name.c_str(), plotSize - ImVec2(2 * offset, 2 * offset), plotFlags)) {
            // TODO: refactor this into a function
            [](decltype(plot) &plot) {
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

                    const auto &port = const_cast<const Block *>(source->block)->outputs()[source->port];

                    if (port.dataSet.empty()) {
                        // Plot one single dummy value so that the sink shows up in the plot legend
                        float v = 0;
                        if (source->visible) {
                            ImPlot::PlotLine(source->name.c_str(), &v, 1);
                        }
                    } else {
                        switch (port.type) {
                        case DigitizerUi::DataType::Float32: {
                            if (source->visible) {
                                auto values = port.dataSet.asFloat32();
                                ImPlot::PlotLine(source->name.c_str(), values.data(), values.size());
                            }
                            break;
                        }
                        default: break;
                        }
                    }

                    // allow legend item labels to be DND sources
                    if (ImPlot::BeginDragDropSourceItem(source->name.c_str())) {
                        DndItem dnd = { &plot, source };
                        ImGui::SetDragDropPayload(dnd_type, &dnd, sizeof(dnd));
                        ImPlot::ItemIcon(color);
                        ImGui::SameLine();
                        ImGui::TextUnformatted(source->name.c_str());
                        ImPlot::EndDragDropSource();
                    }
                }
            }(plot);
            auto acceptSource = [&]() {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(dnd_type)) {
                    auto *dnd = static_cast<DndItem *>(payload->Data);
                    plot.sources.push_back(dnd->source);
                    if (auto *plot = dnd->plotSource) {
                        plot->sources.erase(std::find(plot->sources.begin(), plot->sources.end(), dnd->source));
                    }
                }
            };

            // allow the main plot area to be a DND target
            if (ImPlot::BeginDragDropTargetPlot()) {
                acceptSource();
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
                            break;
                        }
                    }
                }
            }

            ImPlot::EndPlot();
            ImPlot::PopStyleVar(2);

            if (mode == Mode::Layout) {
                if (frameHovered) {
                    ImGui::PushFont(app->fontIcons);
                    ImGui::SetCursorPos(pos + plotPos + ImVec2(plotSize.x, 0) - ImVec2(30, -15));
                    ImGui::PushID(plot.name.c_str());
                    if (ImGui::Button("\uf2ed")) {
                        toDelete = &plot;
                    }
                    ImGui::PopID();
                    ImGui::PopFont();
                }

                auto action = getAction(frameHovered, !plotItemHovered, screenPos, plotPos, plotSize);
                if (action != Action::None) {
                    clickedAction = action;
                    clickedPlot   = &plot;
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
        const ImVec2 cursorPos = ImGui::GetCursorPos();

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
            signal.visible = !signal.visible;
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
                ImGui::SameLine();  // keep item on the same line if compatible with overall pane width
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
