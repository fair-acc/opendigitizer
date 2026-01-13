#include "DashboardPage.hpp"

#include <format>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <implot.h>
#include <memory>
#include <print>

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

#include "components/SignalLegend.hpp"
#include "components/Splitter.hpp"

#include "blocks/ImPlotSink.hpp"
#include "blocks/RemoteSource.hpp"
#include "charts/Chart.hpp"
#include "charts/SignalSink.hpp"
#include "charts/SinkRegistry.hpp"

namespace DigitizerUi {

namespace {
constexpr inline auto kMaxPlots   = 16u;
constexpr inline auto kGridWidth  = 16u;
constexpr inline auto kGridHeight = 16u;
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

DashboardPage::DashboardPage() {
    // Use SinkRegistry for listening to sink registration events
    opendigitizer::charts::SinkRegistry::instance().addListener(this, [this](opendigitizer::charts::SignalSink& sink, bool wasAdded) {
        if (!m_dashboard || !wasAdded || _addedSourceBlocksWaitingForSink.empty()) {
            return;
        }

        auto it = std::ranges::find_if(_addedSourceBlocksWaitingForSink, [&sink](const auto& kvp) { return kvp.second.signalData.signalName == sink.signalName(); });
        if (it == _addedSourceBlocksWaitingForSink.end()) {
            std::print("[DashboardPage] Status: A sink added that is not connected to a remote source\n");
            return;
        }

        auto url             = it->first;
        auto sourceInWaiting = it->second;
        _addedSourceBlocksWaitingForSink.erase(it);

        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.endpoint    = gr::scheduler::property::kEmplaceEdge;
        message.serviceName = m_dashboard->graphModel().rootBlock.ownerSchedulerUniqueName();
        message.data        = gr::property_map{                                                                     //
            {std::string(gr::serialization_fields::EDGE_SOURCE_BLOCK), sourceInWaiting.sourceBlockName},     //
            {std::string(gr::serialization_fields::EDGE_SOURCE_PORT), "out"},                                //
            {std::string(gr::serialization_fields::EDGE_DESTINATION_BLOCK), std::string(sink.uniqueName())}, //
            {std::string(gr::serialization_fields::EDGE_DESTINATION_PORT), "in"},                            //
            {std::string(gr::serialization_fields::EDGE_MIN_BUFFER_SIZE), gr::Size_t(4096)},                 //
            {std::string(gr::serialization_fields::EDGE_WEIGHT), 1},                                         //
            {std::string(gr::serialization_fields::EDGE_NAME), std::string()}};
        m_dashboard->graphModel().sendMessage(std::move(message));

        auto& plot = m_dashboard->newPlot(0, 0, 1, 1);
        plot.sourceNames.push_back(sourceInWaiting.signalData.signalName);
        m_dashboard->loadPlotSourcesFor(plot);
    });
}

DashboardPage::~DashboardPage() { opendigitizer::charts::SinkRegistry::instance().removeListener(this); }

void DashboardPage::draw(Mode mode) noexcept {
    // Process any deferred chart transmutation before rendering
    processPendingTransmutation();

    const float  left = ImGui::GetCursorPosX();
    const float  top  = ImGui::GetCursorPosY();
    const ImVec2 size = ImGui::GetContentRegionAvail();

    const bool      horizontalSplit   = size.x > size.y;
    constexpr float splitterWidth     = 6;
    constexpr float halfSplitterWidth = splitterWidth / 2.f;
    const float     ratio             = components::Splitter(size, horizontalSplit, splitterWidth, 0.2f, !m_editPane.selectedBlock());

    ImGui::SetCursorPosX(left);
    ImGui::SetCursorPosY(top);

    {
        IMW::Child plotsChild("##plots", horizontalSplit ? ImVec2(size.x * (1.f - ratio) - halfSplitterWidth, size.y) : ImVec2(size.x, size.y * (1.f - ratio) - halfSplitterWidth), false, ImGuiWindowFlags_NoScrollbar);

        if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_editPane.setSelectedBlock(nullptr, nullptr);
        }

        // Plots
        {
            IMW::Group group;
            drawPlots(mode);
        }
        ImGui::SetCursorPos(ImVec2(0, ImGui::GetWindowHeight() - legend_box.y));

        // Legend
        {
            IMW::Group group;
            // Button strip
            if (mode == Mode::Layout) {
                if (plotButton("\uF201", "create new chart")) {
                    _showNewPlotModal = true;
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

            drawGlobalLegend(mode);

            if (m_dashboard && m_remoteSignalSelector) {
                // Post button strip
                if (mode == Mode::Layout) {
                    ImGui::SameLine();
                    if (plotButton("\uf067", "add signal")) {
                        // 'plus' button in the global legend, adds a new signal
                        // to the dashboard
                        m_remoteSignalSelector->open();
                    }

                    for (const auto& selectedRemoteSignal : m_remoteSignalSelector->drawAndReturnSelected()) {
                        const auto& uriStr_           = selectedRemoteSignal.uri();
                        _addingRemoteSignals[uriStr_] = selectedRemoteSignal;

                        m_dashboard->addRemoteSignal(selectedRemoteSignal);

                        opendigitizer::RemoteSourceManager::instance().setRemoteSourceAddedCallback(uriStr_, [this, selectedRemoteSignal](opendigitizer::RemoteSourceModel& remoteSource) {
                            const auto& uriStr = selectedRemoteSignal.uri();
                            // Switching state for the signal -- from "adding the source block"
                            // to "waiting for sink block to be created"
                            _addedSourceBlocksWaitingForSink[uriStr] = SourceBlockInWaiting{
                                .signalData      = std::move(_addingRemoteSignals[uriStr]), //
                                .sourceBlockName = remoteSource.uniqueName()                //
                            };
                            _addingRemoteSignals.erase(uriStr);

                            // Can be opendigitizer::RemoteStreamSource<float32> or opendigitizer::RemoteDataSetSource<float32>
                            // for the time being, but let's support double (float64) out of the box as well
                            const std::string remoteSourceType = remoteSource.typeName();

                            auto                   it = std::ranges::find(remoteSourceType, '<');
                            const std::string_view remoteSourceBaseType(remoteSourceType.begin(), it);
                            const std::string_view remoteSourceTypeParams(it, remoteSourceType.end());

                            std::string sinkBlockType   = "opendigitizer::ImPlotSink";
                            std::string sinkBlockParams =                                                                                                             //
                                (remoteSourceBaseType == "opendigitizer::RemoteStreamSource" && remoteSourceTypeParams == "<float32>")    ? "<float32>"s              //
                                : (remoteSourceBaseType == "opendigitizer::RemoteStreamSource" && remoteSourceTypeParams == "<float64>")  ? "<float64>"s              //
                                : (remoteSourceBaseType == "opendigitizer::RemoteDataSetSource" && remoteSourceTypeParams == "<float32>") ? "<gr::DataSet<float32>>"s //
                                : (remoteSourceBaseType == "opendigitizer::RemoteDataSetSource" && remoteSourceTypeParams == "<float64>") ? "<gr::DataSet<float64>>"s //
                                                                                                                                          : /* otherwise error */ ""s;

                            gr::Message message;
                            message.cmd      = gr::message::Command::Set;
                            message.endpoint = gr::scheduler::property::kEmplaceBlock;
                            // The root block needs to be a scheduler
                            message.serviceName = m_dashboard->graphModel().rootBlock.ownerSchedulerUniqueName();
                            message.data        = gr::property_map{
                                //
                                {"type"s, sinkBlockType + sinkBlockParams}, //
                                {
                                    "properties"s,
                                    gr::property_map{
                                        //
                                        {"signal_name"s, selectedRemoteSignal.signalName}, //
                                        {"signal_unit"s, selectedRemoteSignal.unit}        //
                                    } //
                                } //
                            };
                            m_dashboard->graphModel().sendMessage(std::move(message));
                        });
                    }
                }
            }

            if (LookAndFeel::instance().prototypeMode) {
                ImGui::SameLine();
                // Retrieve FPS and milliseconds per iteration
                const float fps     = ImGui::GetIO().Framerate;
                const auto  str     = std::format("FPS:{:5.0f}({:2}ms)", fps, LookAndFeel::instance().execTime.count());
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

    // Modal dialogs
    drawNewPlotModal();
}

void DashboardPage::drawPlots(DigitizerUi::DashboardPage::Mode mode) {
    pane_size = ImGui::GetContentRegionAvail();
    pane_size.y -= legend_box.y;

    const float w = pane_size.x / float(kGridWidth);
    const float h = pane_size.y / float(kGridHeight);

    if (mode == Mode::Layout) {
        drawGrid(w, h);
    }

    DockSpace::Windows windows;

    // Iterate uiGraph blocks filtered by ChartPane category
    for (auto& blockPtr : m_dashboard->uiGraph().blocks()) {
        if (blockPtr->uiCategory() != gr::UICategory::ChartPane) {
            continue;
        }

        // Get or create UIWindow for this block (lazy creation)
        auto& uiWindow = m_dashboard->getOrCreateUIWindow(blockPtr);
        if (!uiWindow.window) {
            continue;
        }

        windows.push_back(uiWindow.window);
        uiWindow.window->renderFunc = [this, &blockPtr, mode] {
            // Charts invoke work on their sinks internally in draw()
            // Pass layoutMode so chart can show/hide legend appropriately
            gr::property_map drawConfig;
            drawConfig["layoutMode"] = (mode == Mode::Layout);
            blockPtr->draw(drawConfig);
        };
    }

    m_dockSpace.render(windows, pane_size);
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

void DashboardPage::drawGlobalLegend([[maybe_unused]] const DashboardPage::Mode& mode) noexcept {
    alignForWidth(std::max(10.f, legend_box.x), 0.5f);
    legend_box.x = 0.f;

    // Use SignalLegend component (D&D is handled internally via DndHelper)
    legend_box = components::SignalLegend::draw(pane_size.x,
        // Right-click callback: open settings panel
        [this](std::string_view sinkUniqueName) {
            m_editPane.setSelectedBlock(m_dashboard->graphModel().rootBlock.findBlockByUniqueName(std::string(sinkUniqueName)), std::addressof(m_dashboard->graphModel()));
            m_editPane.closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay;
        });
}

DigitizerUi::Dashboard::Plot* DashboardPage::newPlot(std::string_view chartType) {
    if (m_dashboard->plots().size() < kMaxPlots) {
        return std::addressof(m_dashboard->newPlot(0, 0, 1, 1, chartType));
    }
    return nullptr;
}

void DashboardPage::drawNewPlotModal() {
    using namespace opendigitizer::charts;

    if (!_showNewPlotModal) {
        return;
    }

    ImGui::OpenPopup("New Chart");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("New Chart", &_showNewPlotModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Select chart type:");
        ImGui::Separator();

        auto chartTypes = opendigitizer::charts::registeredChartTypes();
        for (const auto& type : chartTypes) {
            bool selected = (_selectedChartType == type);
            if (ImGui::Selectable(type.c_str(), selected)) {
                _selectedChartType = type;
            }
        }

        ImGui::Separator();

        if (ImGui::Button("Create", ImVec2(120, 0))) {
            newPlot(_selectedChartType);
            _showNewPlotModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            _showNewPlotModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void DashboardPage::setLayoutType(DockingLayoutType type) { m_dockSpace.setLayoutType(type); }

} // namespace DigitizerUi
