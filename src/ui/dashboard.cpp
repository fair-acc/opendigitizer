
#include "dashboard.h"

#include <fmt/format.h>
#include <imgui.h>
#include <implot.h>

#include "datasink.h"
#include "flowgraph.h"

namespace DigitizerUi {

struct Dashboard::Plot {
    Plot() {
        static int n = 1;
        name         = fmt::format("Plot {}", n++);
    }

    std::string             name;
    std::vector<DataSink *> sinks;
};

Dashboard::Dashboard(DigitizerUi::FlowGraph *fg)
    : m_flowGraph(fg) {
    m_plots.resize(2);
}

Dashboard::~Dashboard() {
}

void Dashboard::draw() {
    // ImPlot::ShowDemoWindow();

    m_flowGraph->update();

    // child window to serve as initial source for our DND items
    ImGui::BeginChild("DND_LEFT", ImVec2(100, 400));

    struct DndItem {
        Plot     *plotSource;
        DataSink *sink;
    };

    static constexpr auto dndType = "DND_SINK";

    for (auto &b : m_flowGraph->sinkBlocks()) {
        auto *s = static_cast<DataSink *>(b.get());

        ImPlot::ItemIcon(s->color);
        ImGui::SameLine();
        ImGui::Selectable(s->name.c_str(), false, 0, ImVec2(100, 0));
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            DndItem dnd = { nullptr, s };
            ImGui::SetDragDropPayload(dndType, &dnd, sizeof(dnd));
            ImPlot::ItemIcon(s->color);
            ImGui::SameLine();
            ImGui::TextUnformatted(s->name.c_str());
            ImGui::EndDragDropSource();
        }
    }
    ImGui::EndChild();

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(dndType)) {
            auto *dnd = static_cast<DndItem *>(payload->Data);
            if (auto plot = dnd->plotSource) {
                plot->sinks.erase(std::find(plot->sinks.begin(), plot->sinks.end(), dnd->sink));
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    ImGui::BeginChild("DND_RIGHT");

    for (auto &plot : m_plots) {
        if (ImPlot::BeginPlot(plot.name.c_str())) {
            for (auto *sink : plot.sinks) {
                ImPlot::SetNextLineStyle(sink->color);

                if (!sink->hasData) {
                    // Plot one single dummy value so that the sink shows up in the plot legend
                    float v = 0;
                    ImPlot::PlotLine(sink->name.c_str(), &v, 1);
                } else {
                    switch (sink->dataType) {
                    case DigitizerUi::DataType::Float32: {
                        auto values = sink->data.asFloat32();
                        ImPlot::PlotLine(sink->name.c_str(), values.data(), values.size());
                        break;
                    }
                    default: break;
                    }
                }

                // allow legend item labels to be DND sources
                if (ImPlot::BeginDragDropSourceItem(sink->name.c_str())) {
                    DndItem dnd = { &plot, sink };
                    ImGui::SetDragDropPayload(dndType, &dnd, sizeof(dnd));
                    ImPlot::ItemIcon(sink->color);
                    ImGui::SameLine();
                    ImGui::TextUnformatted(sink->name.c_str());
                    ImPlot::EndDragDropSource();
                }
            }

            auto acceptSink = [&]() {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(dndType)) {
                    auto *dnd = static_cast<DndItem *>(payload->Data);
                    plot.sinks.push_back(dnd->sink);
                    if (auto plot = dnd->plotSource) {
                        plot->sinks.erase(std::find(plot->sinks.begin(), plot->sinks.end(), dnd->sink));
                    }
                }
            };

            // allow the main plot area to be a DND target
            if (ImPlot::BeginDragDropTargetPlot()) {
                acceptSink();
                ImPlot::EndDragDropTarget();
            }

            ImPlot::EndPlot();
        }
    }
    ImGui::EndChild();
}

} // namespace DigitizerUi
