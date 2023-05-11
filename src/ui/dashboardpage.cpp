
#include "dashboardpage.h"

#include <fmt/format.h>
#include <imgui.h>
#include <implot.h>

#include "app.h"
#include "dashboard.h"
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

constexpr int gridSizeW = 16;
constexpr int gridSizeH = 16;

} // namespace

DashboardPage::DashboardPage() {
}

DashboardPage::~DashboardPage() {
}

void DashboardPage::draw(App *app, Dashboard *dashboard, Mode mode) {
    // ImPlot::ShowDemoWindow();

    struct DndItem {
        Dashboard::Plot   *plotSource;
        Dashboard::Source *source;
    };

    static constexpr auto dndType = "DND_SOURCE";

    if (mode == Mode::Layout) {
        // child window to serve as initial source for our DND items
        ImGui::BeginChild("DND_LEFT", ImVec2(150, 400));

        if (ImGui::Button("New plot")) {
            newPlot(dashboard);
        }

        for (auto &s : dashboard->sources()) {
            auto color = ImGui::ColorConvertU32ToFloat4(s.color);
            ImPlot::ItemIcon(color);
            ImGui::SameLine();
            ImGui::Selectable(s.name.c_str(), false, 0, ImVec2(150, 0));
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                DndItem dnd = { nullptr, &s };
                ImGui::SetDragDropPayload(dndType, &dnd, sizeof(dnd));
                ImPlot::ItemIcon(color);
                ImGui::SameLine();
                ImGui::TextUnformatted(s.name.c_str());
                ImGui::EndDragDropSource();
            }
        }
        ImGui::EndChild();

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(dndType)) {
                auto *dnd = static_cast<DndItem *>(payload->Data);
                if (auto plot = dnd->plotSource) {
                    plot->sources.erase(std::find(plot->sources.begin(), plot->sources.end(), dnd->source));
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
    }

    ImGui::BeginChild("DND_RIGHT");

    auto  size = ImGui::GetContentRegionAvail();
    float w    = size.x / float(gridSizeW);
    float h    = size.y / float(gridSizeH);

    const uint32_t gridLineColor = App::instance().style() == Style::Light ? 0x40000000 : 0x40ffffff;

    if (mode == Mode::Layout) {
        auto pos = ImGui::GetCursorScreenPos();
        for (float x = pos.x; x < pos.x + size.x; x += w) {
            ImGui::GetWindowDrawList()->AddLine({ x, pos.y }, { x, pos.y + size.y }, gridLineColor);
        }
        for (float y = pos.y; y < pos.y + size.y; y += h) {
            ImGui::GetWindowDrawList()->AddLine({ pos.x, y }, { pos.x + size.x, y }, gridLineColor);
        }
    }

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

    for (auto &plot : dashboard->plots()) {
        const int offset       = mode == Mode::Layout ? 5 : 0;
        const int offset2      = offset * 2;

        auto      plotPos      = ImVec2(w * plot.rect.x, h * plot.rect.y);
        auto      plotSize     = ImVec2{ plot.rect.w * w, plot.rect.h * h };

        bool      frameHovered = [&]() {
            if (mode != Mode::Layout) {
                return false;
            }

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
        if (ImPlot::BeginPlot(plot.name.c_str(), plotSize - ImVec2(offset2, offset2), ImPlotFlags_NoChild)) {
            for (const auto &a : plot.axes) {
                auto axis = a.axis == Dashboard::Plot::Axis::X ? ImAxis_X1 : ImAxis_Y1;
                ImPlot::SetupAxis(axis);
                if (a.min < a.max) {
                    ImPlot::SetupAxisLimits(axis, a.min, a.max);
                }
            }
            ImPlot::SetupFinish();

            for (auto *source : plot.sources) {
                auto color = ImGui::ColorConvertU32ToFloat4(source->color);
                ImPlot::SetNextLineStyle(color);

                const auto &port = const_cast<const Block *>(source->block)->outputs()[source->port];

                if (port.dataSet.empty()) {
                    // Plot one single dummy value so that the sink shows up in the plot legend
                    float v = 0;
                    ImPlot::PlotLine(source->name.c_str(), &v, 1);
                } else {
                    switch (port.type) {
                    case DigitizerUi::DataType::Float32: {
                        auto values = port.dataSet.asFloat32();
                        ImPlot::PlotLine(source->name.c_str(), values.data(), values.size());
                        break;
                    }
                    default: break;
                    }
                }

                // allow legend item labels to be DND sources
                if (ImPlot::BeginDragDropSourceItem(source->name.c_str())) {
                    DndItem dnd = { &plot, source };
                    ImGui::SetDragDropPayload(dndType, &dnd, sizeof(dnd));
                    ImPlot::ItemIcon(color);
                    ImGui::SameLine();
                    ImGui::TextUnformatted(source->name.c_str());
                    ImPlot::EndDragDropSource();
                }
            }

            auto acceptSource = [&]() {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(dndType)) {
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

            if (mode == Mode::Layout) {
                if (frameHovered) {
                    ImGui::PushFont(app->fontIcons);
                    ImGui::SetCursorPos(pos + plotPos + ImVec2(8, 8));
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
        }
    }
    ImGui::EndChild();

    if (toDelete) {
        dashboard->deletePlot(toDelete);
    }
}

void DashboardPage::newPlot(Dashboard *dashboard) {
    bool grid[gridSizeW][gridSizeH];
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
                if (x2 == gridSizeW || y2 == gridSizeH || grid[x2][y2]) {
                    return false;
                }
            }
        }
        return true;
    };

    auto findRectangle = [&](int &rx, int &ry, int &rw, int &rh) {
        int i = 0;
        while (true) {
            for (int y = 0; y < gridSizeH; ++y) {
                for (int x = 0; x < gridSizeW; ++x) {
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

    int w = gridSizeW / 2;
    int h = w * 6. / 8.;

    int x, y;
    if (findRectangle(x, y, w, h)) {
        dashboard->newPlot(x, y, w, h);
    }
}

} // namespace DigitizerUi
