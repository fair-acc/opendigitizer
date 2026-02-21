#include "DashboardPage.hpp"

#include <format>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <implot.h>
#include <memory>
#include <print>

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

#include "components/Splitter.hpp"

#include "blocks/GlobalSignalLegend.hpp"
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
        if (!_dashboard || !wasAdded || _addedSourceBlocksWaitingForSink.empty()) {
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
        message.serviceName = _dashboard->graphModel.rootBlock.ownerSchedulerUniqueName();
        message.data        = gr::property_map{                                                                          //
            {std::pmr::string(gr::serialization_fields::EDGE_SOURCE_BLOCK), sourceInWaiting.sourceBlockName},     //
            {std::pmr::string(gr::serialization_fields::EDGE_SOURCE_PORT), "out"},                                //
            {std::pmr::string(gr::serialization_fields::EDGE_DESTINATION_BLOCK), std::string(sink.uniqueName())}, //
            {std::pmr::string(gr::serialization_fields::EDGE_DESTINATION_PORT), "in"},                            //
            {std::pmr::string(gr::serialization_fields::EDGE_MIN_BUFFER_SIZE), gr::Size_t(4096)},                 //
            {std::pmr::string(gr::serialization_fields::EDGE_WEIGHT), 1},                                         //
            {std::pmr::string(gr::serialization_fields::EDGE_NAME), std::string()}};
        _dashboard->graphModel.sendMessage(std::move(message));

        auto& uiWindow = _dashboard->newUIBlock(0, 0, 1, 1);
        auto  names    = grc_compat::getBlockSinkNames(uiWindow.block.get());
        names.push_back(sourceInWaiting.signalData.signalName);
        grc_compat::setBlockSinkNames(uiWindow.block.get(), names);
    });
}

DashboardPage::~DashboardPage() { opendigitizer::charts::SinkRegistry::instance().removeListener(this); }

void DashboardPage::draw(Mode mode) noexcept {
    processPendingTransmutation();
    processPendingRemovals();

    const float  left = ImGui::GetCursorPosX();
    const float  top  = ImGui::GetCursorPosY();
    const ImVec2 size = ImGui::GetContentRegionAvail();

    const bool      horizontalSplit   = size.x > size.y;
    constexpr float splitterWidth     = 6;
    constexpr float halfSplitterWidth = splitterWidth / 2.f;
    const float     ratio             = components::Splitter(size, horizontalSplit, splitterWidth, 0.2f, !_editPane.selectedBlock());

    ImGui::SetCursorPosX(left);
    ImGui::SetCursorPosY(top);

    {
        IMW::Child plotsChild("##plots", horizontalSplit ? ImVec2(size.x * (1.f - ratio) - halfSplitterWidth, size.y) : ImVec2(size.x, size.y * (1.f - ratio) - halfSplitterWidth), false, ImGuiWindowFlags_NoScrollbar);

        if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            _editPane.setSelectedBlock(nullptr, nullptr);
        }

        // Render ChartPane blocks
        {
            IMW::Group group;

            _paneSize = ImGui::GetContentRegionAvail();
            _paneSize.y -= _legendBox.y;

            const float w = _paneSize.x / float(kGridWidth);
            const float h = _paneSize.y / float(kGridHeight);

            // Draw layout grid in Layout mode
            if (mode == Mode::Layout) {
                const uint32_t gridLineColor = LookAndFeel::instance().style == LookAndFeel::Style::Light ? 0x40000000 : 0x40ffffff;
                auto           pos           = ImGui::GetCursorScreenPos();
                for (float x = pos.x; x < pos.x + _paneSize.x; x += w) {
                    ImGui::GetWindowDrawList()->AddLine({x, pos.y}, {x, pos.y + _paneSize.y}, gridLineColor);
                }
                for (float y = pos.y; y < pos.y + _paneSize.y; y += h) {
                    ImGui::GetWindowDrawList()->AddLine({pos.x, y}, {pos.x + _paneSize.x, y}, gridLineColor);
                }
            }

            DockSpace::Windows windows;

            // Iterate uiGraph blocks filtered by ChartPane category
            for (auto& blockPtr : _dashboard->uiGraph.blocks()) {
                if (blockPtr->uiCategory() != gr::UICategory::Content) {
                    continue;
                }

                // Get or create UIWindow for this block (lazy creation)
                auto& uiWindow = _dashboard->getOrCreateUIWindow(blockPtr);
                if (!uiWindow.window) {
                    continue;
                }

                windows.push_back(uiWindow.window);
                // Capture shared_ptr by value to ensure block stays alive during render
                uiWindow.window->renderFunc = [this, block = blockPtr, mode] {
                    gr::property_map drawConfig;
                    drawConfig["layoutMode"] = (mode == Mode::Layout);
                    block->draw(drawConfig);
                };
            }

            _dockSpace.render(windows, _paneSize);
        }
        ImGui::SetCursorPos(ImVec2(0, ImGui::GetWindowHeight() - _legendBox.y));

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
                    _dockSpace.setLayoutType(DockingLayoutType::Row);
                }
                ImGui::SameLine();
                if (plotButton("\uF7A4", "change to the vertical layout")) {
                    _dockSpace.setLayoutType(DockingLayoutType::Column);
                }
                ImGui::SameLine();
                if (plotButton("\uF58D", "change to the grid layout")) {
                    _dockSpace.setLayoutType(DockingLayoutType::Grid);
                }
                ImGui::SameLine();
                if (plotButton("\uF248", "change to the free layout")) {
                    _dockSpace.setLayoutType(DockingLayoutType::Free);
                }
                ImGui::SameLine();
            }

            // Render Toolbar blocks (legend, etc.) - centre-aligned
            {
                static constexpr std::string_view kLegendBlockType = "DigitizerUi::GlobalSignalLegend";

                alignForWidth(std::max(10.f, _legendBox.x), 0.5f);
                _legendBox.x = 0.f;

                ImVec2 totalSize{0.f, 0.f};
                for (auto& blockPtr : _dashboard->uiGraph.blocks()) {
                    if (blockPtr->uiCategory() != gr::UICategory::Toolbar) {
                        continue;
                    }

                    // Configure GlobalSignalLegend before drawing
                    const bool isLegendBlock = (blockPtr->typeName() == kLegendBlockType);
                    if (isLegendBlock) {
                        auto* legendBlock = static_cast<GlobalSignalLegend*>(blockPtr->raw());
                        legendBlock->setPaneWidth(_paneSize.x);
                        legendBlock->setRightClickCallback([this](std::string_view sinkUniqueName) {
                            auto found = _dashboard->graphModel.recursiveFindBlockByUniqueName(std::string(sinkUniqueName));
                            if (found) {
                                _editPane.setSelectedBlock(found.block, std::addressof(_dashboard->graphModel));
                                _editPane.closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay;
                            }
                        });
                    }

                    // Draw the toolbar block
                    gr::property_map drawConfig;
                    drawConfig["paneWidth"] = _paneSize.x;
                    blockPtr->draw(drawConfig);

                    // Track legend size
                    if (isLegendBlock) {
                        auto* legendBlock = static_cast<GlobalSignalLegend*>(blockPtr->raw());
                        totalSize         = legendBlock->legendSize();
                    }
                }

                _legendBox = totalSize;
            }

            if (_dashboard && _remoteSignalSelector) {
                // Post button strip
                if (mode == Mode::Layout) {
                    ImGui::SameLine();
                    if (plotButton("\uf067", "add signal")) {
                        // 'plus' button in the global legend, adds a new signal
                        // to the dashboard
                        _remoteSignalSelector->open();
                    }

                    for (const auto& selectedRemoteSignal : _remoteSignalSelector->drawAndReturnSelected()) {
                        const auto& uriStr_           = selectedRemoteSignal.uri();
                        _addingRemoteSignals[uriStr_] = selectedRemoteSignal;

                        _dashboard->addRemoteSignal(selectedRemoteSignal);

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
                            message.serviceName = _dashboard->graphModel.rootBlock.ownerSchedulerUniqueName();
                            message.data        = gr::property_map{
                                       {"type", sinkBlockType + sinkBlockParams}, //
                                       {
                                    "properties",
                                    gr::property_map{
                                               {"signal_name", selectedRemoteSignal.signalName}, //
                                               {"signal_unit", selectedRemoteSignal.unit}        //
                                    } //
                                } //
                            };
                            _dashboard->graphModel.sendMessage(std::move(message));
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
        _legendBox.y = std::floor(ImGui::GetItemRectSize().y * 1.5f);
    }

    if (horizontalSplit) {
        const float w = size.x * ratio;
        components::BlockControlsPanel(_editPane, {left + size.x - w + halfSplitterWidth, top}, {w - halfSplitterWidth, size.y}, true);
    } else {
        const float h = size.y * ratio;
        components::BlockControlsPanel(_editPane, {left, top + size.y - h + halfSplitterWidth}, {size.x, h - halfSplitterWidth}, false);
    }

    // Modal dialogs
    drawNewPlotModal();
}

DigitizerUi::Dashboard::UIWindow* DashboardPage::newUIBlock(std::string_view chartType) {
    if (_dashboard->uiWindows.size() < kMaxPlots) {
        return std::addressof(_dashboard->newUIBlock(0, 0, 1, 1, chartType));
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
            newUIBlock(_selectedChartType);
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

void DashboardPage::setLayoutType(DockingLayoutType type) { _dockSpace.setLayoutType(type); }

} // namespace DigitizerUi
