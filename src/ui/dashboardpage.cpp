
#include "dashboardpage.h"

#include <fmt/format.h>
#include <imgui.h>
#include <implot.h>

#include "dashboard.h"
#include "flowgraph.h"
#include "flowgraph/datasink.h"

namespace DigitizerUi {

DashboardPage::DashboardPage(DigitizerUi::FlowGraph *fg)
    : m_flowGraph(fg) {
}

DashboardPage::~DashboardPage() {
}

void DashboardPage::draw(Dashboard *dashboard) {
    // ImPlot::ShowDemoWindow();

    m_flowGraph->update();

    // child window to serve as initial source for our DND items
    ImGui::BeginChild("DND_LEFT", ImVec2(150, 400));

    struct DndItem {
        Dashboard::Plot   *plotSource;
        Dashboard::Source *source;
    };

    static constexpr auto dndType = "DND_SOURCE";

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
    ImGui::BeginChild("DND_RIGHT");

    for (auto &plot : dashboard->plots()) {
        if (ImPlot::BeginPlot(plot.name.c_str())) {
            for (auto *source : plot.sources) {
                auto color = ImGui::ColorConvertU32ToFloat4(source->color);
                ImPlot::SetNextLineStyle(color);

                const auto &port = const_cast<const Block *>(source->block)->outputs()[source->port];
                switch (port.type) {
                case DigitizerUi::DataType::Float32: {
                    auto values = port.dataSet.asFloat32();
                    ImPlot::PlotLine(source->name.c_str(), values.data(), values.size());
                    break;
                }
                default: break;
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

            ImPlot::EndPlot();
        }
    }
    ImGui::EndChild();
}

} // namespace DigitizerUi
