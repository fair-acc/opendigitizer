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
#include "components/YesNoPopup.hpp"

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

static bool plotButton(const char* glyph, const char* tooltip, float buttonSize) noexcept {
    const bool ret = [&] {
        IMW::StyleColor buttonStyle(ImGuiCol_Button, LookAndFeel::instance().palette().mainWindowButtonBgInactive);
        IMW::StyleColor textStyle(ImGuiCol_Text, LookAndFeel::instance().palette().mainWindowButtonIcon);
        IMW::StyleColor buttonActiveStyle(ImGuiCol_ButtonActive, LookAndFeel::instance().palette().mainWindowButtonBgActive);
        IMW::StyleColor buttonHoveredStyle(ImGuiCol_ButtonHovered, LookAndFeel::instance().palette().mainWindowButtonBgHovered);
        IMW::Font       font(LookAndFeel::instance().fontIconsSolidLarge);
        return ImGui::Button(glyph, {buttonSize, buttonSize});
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
            {std::pmr::string(gr::serialization_fields::EDGE_NAME), "edge"}};
        _dashboard->graphModel.sendMessage(std::move(message));

        auto& uiWindow = _dashboard->newUIBlock(0, 0, 1, 1);
        auto  names    = grc_compat::getBlockSinkNames(uiWindow.block.get());
        names.push_back(sourceInWaiting.signalData.signalName);
        grc_compat::setBlockSinkNames(uiWindow.block.get(), names);
    });
}

DashboardPage::~DashboardPage() { opendigitizer::charts::SinkRegistry::instance().removeListener(this); }

ImVec2 DashboardPage::drawCharts(Mode mode) noexcept {
    IMW::Group group;

    ImVec2 paneSize = ImGui::GetContentRegionAvail();
    paneSize.y -= _legendBox.y;

    const float w = paneSize.x / float(kGridWidth);
    const float h = paneSize.y / float(kGridHeight);

    // Draw layout grid in Layout mode
    if (mode == Mode::Layout) {
        const uint32_t gridLineColor = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().gridLines);
        auto           pos           = ImGui::GetCursorScreenPos();
        float          x             = pos.x;
        while (x < pos.x + paneSize.x) {
            ImGui::GetWindowDrawList()->AddLine({x, pos.y}, {x, pos.y + paneSize.y}, gridLineColor);
            x += w;
        }
        float y = pos.y;
        while (y < pos.y + paneSize.y) {
            ImGui::GetWindowDrawList()->AddLine({pos.x, y}, {pos.x + paneSize.x, y}, gridLineColor);
            y += w;
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
            drawConfig["chartMode"] = magic_enum::enum_name(mode);
            block->draw(drawConfig);
        };

        uiWindow.window->renderDockingContextMenuFunc = [block = blockPtr] {
            opendigitizer::charts::drawDuplicateChartMenuItem(block->uniqueName());
            opendigitizer::charts::drawRemoveChartMenuItem(block->uniqueName());
        };
    }

    _dockSpace.render(windows, paneSize, mode == Mode::Layout);
    return paneSize;
}

void DashboardPage::drawToolbarLayoutButtons(float plotButtonSize) noexcept {
    using enum DigitizerUi::DockingLayoutType;
    IMW::Group layout;
    if (plotButton("\u{F7A5}", "change to the horizontal layout", plotButtonSize)) {
        _dockSpace.setLayoutType(Row);
    }
    ImGui::SameLine();
    if (plotButton("\u{F7A4}", "change to the vertical layout", plotButtonSize)) {
        _dockSpace.setLayoutType(Column);
    }
    ImGui::SameLine();
    if (plotButton("\u{F58D}", "change to the grid layout", plotButtonSize)) {
        _dockSpace.setLayoutType(Grid);
    }
    ImGui::SameLine();
    if (plotButton("\u{F248}", "change to the free layout", plotButtonSize)) {
        _dockSpace.setLayoutType(Free);
    }
    ImGui::SameLine();
}

ImVec2 DashboardPage::drawLegendCenter(Mode mode, ImVec2 chartPaneSize) noexcept {
    static constexpr std::string_view kLegendBlockType = "DigitizerUi::GlobalSignalLegend";

    ImVec2 totalSize{0.f, 0.f};
    for (auto& blockPtr : _dashboard->uiGraph.blocks()) {
        if (blockPtr->uiCategory() != gr::UICategory::Toolbar) {
            continue;
        }

        // Configure GlobalSignalLegend before drawing
        const bool isLegendBlock = (blockPtr->typeName() == kLegendBlockType);
        if (isLegendBlock) {
            auto* legendBlock = static_cast<GlobalSignalLegend*>(blockPtr->raw());
            legendBlock->setPaneWidth(chartPaneSize.x);
            legendBlock->setRightClickCallback([this, mode](std::string_view sinkUniqueName) {
                if (mode == Mode::Layout) {
                    return;
                }
                auto found = _dashboard->graphModel.recursiveFindBlockByUniqueName(std::string(sinkUniqueName));
                if (found) {
                    _editPane.setSelectedBlock(found.block, std::addressof(_dashboard->graphModel));
                    _editPane.closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay;
                }
            });
            legendBlock->setDragDropEnabled(mode == Mode::Interaction);
        }

        // Draw the toolbar block
        gr::property_map drawConfig;
        drawConfig["paneWidth"] = chartPaneSize.x;
        blockPtr->draw(drawConfig);

        // Track legend size
        if (isLegendBlock) {
            auto* legendBlock = static_cast<GlobalSignalLegend*>(blockPtr->raw());
            totalSize         = legendBlock->legendSize();
        }
    }
    return totalSize;
}

void DashboardPage::addSelectedRemoteSignal(const SignalData& selectedRemoteSignal) noexcept {
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

DashboardPage::LegendItemClickResult DashboardPage::drawLegend(Mode mode, ImVec2 chartPaneSize) noexcept {
    IMW::Group group;

    LegendItemClickResult clickResult;

    const float plotButtonSize = LookAndFeel::instance().mainWindowIconButtonSize();

    if (mode != Mode::View) {
        namespace dnd = opendigitizer::charts::dnd;
        if (plotButton("\u{F201}", "create new chart", plotButtonSize)) {
            clickResult.shouldOpenNewPlotModal = true;
        }
        const bool dropped = dnd::handleDropTarget(
            [&clickResult](const dnd::Payload& payload) {
                clickResult.sinkForNewPlot = payload.sink_name;
                return true;
            },
            dnd::kPayloadType);
        clickResult.shouldOpenNewPlotModal = clickResult.shouldOpenNewPlotModal || dropped;
        ImGui::SameLine();
    }

    // Render Toolbar blocks (legend, layout buttons, etc.) - centre-aligned
    alignForWidth(std::max(10.f, _legendBox.x), 0.5f);
    _legendBox.x = 0.f;
    if (mode == Mode::Layout) {
        this->drawToolbarLayoutButtons(plotButtonSize);
        _legendBox = ImGui::GetItemRectSize();
    } else {
        _legendBox = this->drawLegendCenter(mode, chartPaneSize);
    }

    if (mode == Mode::Interaction && _dashboard && _remoteSignalSelector) {
        ImGui::SameLine();
        if (plotButton("\u{F067}", "add signal", plotButtonSize)) {
            // 'plus' button in the global legend, adds a new signal
            // to the dashboard
            _remoteSignalSelector->open();
        }

        for (const auto& selectedRemoteSignal : _remoteSignalSelector->drawAndReturnSelected()) {
            this->addSelectedRemoteSignal(selectedRemoteSignal);
        }
    }

    struct Button {
        const char*           icon    = nullptr;
        const char*           tooltip = nullptr;
        std::function<void()> action;
    };

    std::vector<Button> buttons;

    if (mode == Mode::Interaction) {
        buttons.emplace_back("\u{F248}", "Enter layout mode", [this] {
            if (this->_requestSetLayoutMode) {
                this->_requestSetLayoutMode(true);
            }
        });
        buttons.emplace_back("\u{F023}", "Lock the dashboard", [&clickResult] { clickResult.shouldOpenEnterViewOnlyModeModal = true; });
    } else if (mode == Mode::Layout) {
        buttons.emplace_back("\u{F52B}", "Finish layouting", [this] {
            if (this->_requestSetLayoutMode) {
                this->_requestSetLayoutMode(false);
            }
        });
    }

    ImGui::SameLine();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    alignForWidth(spacing + (spacing + plotButtonSize) * static_cast<float>(buttons.size()), 1.0);
    const float cursorBeforeButtons = ImGui::GetCursorPosX();

    for (const auto& button : buttons) {
        if (plotButton(button.icon, button.tooltip, plotButtonSize)) {
            button.action();
        }
        ImGui::SameLine();
    }

    if (LookAndFeel::instance().prototypeMode) {
        ImGui::SameLine();
        // Retrieve FPS and milliseconds per iteration
        const float fps     = ImGui::GetIO().Framerate;
        const auto  str     = std::format("FPS:{:5.0f}({:2}ms)", fps, LookAndFeel::instance().execTime.count());
        const auto  estSize = ImGui::CalcTextSize(str.c_str());
        ImGui::SetCursorPosX(cursorBeforeButtons - estSize.x - spacing);
        ImGui::Text("%s", str.c_str());
    }
    ImGui::Dummy(ImVec2(0.f, 0.f));

    _legendBox.y = std::max(_legendBox.y, plotButtonSize);

    return clickResult;
}

void DashboardPage::draw(Mode mode) noexcept {
    processPendingTransmutation();
    processPendingRemovals();

    const float  left = ImGui::GetCursorPosX();
    const float  top  = ImGui::GetCursorPosY();
    const ImVec2 size = ImGui::GetContentRegionAvail();

    const bool      horizontalSplit   = size.x > size.y;
    constexpr float splitterWidth     = 6;
    constexpr float halfSplitterWidth = splitterWidth / 2.f;
    const float     ratio             = mode == Mode::Interaction ? components::Splitter(size, horizontalSplit, splitterWidth, 0.2f, !_editPane.selectedBlock()) : 0.f;

    ImGui::SetCursorPosX(left);
    ImGui::SetCursorPosY(top);

    LegendItemClickResult legendClickResult;
    {
        IMW::Child plotsChild("##plots", horizontalSplit ? ImVec2(size.x * (1.f - ratio) - halfSplitterWidth, size.y) : ImVec2(size.x, size.y * (1.f - ratio) - halfSplitterWidth), false, ImGuiWindowFlags_NoScrollbar);

        if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            _editPane.setSelectedBlock(nullptr, nullptr);
        }

        // Render ChartPane blocks
        const auto paneSize = this->drawCharts(mode);
        ImGui::SetCursorPos(ImVec2(0, ImGui::GetWindowHeight() - _legendBox.y));

        // quickfix for an imgui bug?: the SetCursorPos above does not seem to
        // be sufficient for getting our cursor to return there after
        // SameLine(). The issue is visible iff we do manual cursor
        // manipulation (as the global signal legend does for the first color
        // rect). So to make sure the first item draws at the same position as
        // the succeeding ones after SameLine(), just insert a dummy size and
        // do SameLine here, so the imgui context is in the same state as later
        ImGui::ItemSize(ImVec2{}, 0.f);
        ImGui::SameLine();

        legendClickResult = this->drawLegend(mode, paneSize);

        if (!legendClickResult.sinkForNewPlot.empty()) {
            _sinkForNewPlot = legendClickResult.sinkForNewPlot;
        }
    }

    if (horizontalSplit) {
        const float w = size.x * ratio;
        components::BlockControlsPanel(_editPane, {left + size.x - w + halfSplitterWidth, top}, {w - halfSplitterWidth, size.y}, true);
    } else {
        const float h = size.y * ratio;
        components::BlockControlsPanel(_editPane, {left, top + size.y - h + halfSplitterWidth}, {size.x, h - halfSplitterWidth}, false);
    }

    // Modal dialogs
    if (legendClickResult.shouldOpenNewPlotModal) {
        ImGui::OpenPopup(addChartPopupID);
    }
    drawNewPlotModal();

    if (legendClickResult.shouldOpenEnterViewOnlyModeModal) {
        ImGui::OpenPopup(enterViewOnlyModePopupID);
    }
    using namespace components;
    if (const auto popup = beginYesNoPopup(enterViewOnlyModePopupID); isPopupOpen(popup)) {
        if (isPopupConfirmed(popup) && this->_requestViewOnlyMode) {
            this->_requestViewOnlyMode();
        }
        ImGui::EndPopup();
    }
}

DigitizerUi::Dashboard::UIWindow* DashboardPage::newUIBlock(std::string_view chartType, std::string_view initialSignal) {
    if (_dashboard->uiWindows.size() < kMaxPlots) {
        gr::property_map chartInitialParameters;
        if (!initialSignal.empty()) {
            chartInitialParameters["data_sinks"] = gr::Tensor<gr::pmt::Value>{initialSignal};
        }
        return std::addressof(_dashboard->newUIBlock(0, 0, 1, 1, chartType, chartInitialParameters));
    }
    return nullptr;
}

void DashboardPage::drawNewPlotModal() {
    using namespace opendigitizer::charts;

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_Appearing);

    bool showCloseButton = true; // just pass this to imgui to cause it to put an (X) button on the popup
    if (auto popup = IMW::ModalPopup(addChartPopupID, &showCloseButton, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Select chart type:");
        ImGui::Separator();

        auto chartTypes = opendigitizer::charts::registeredChartTypes();
        for (const auto& type : chartTypes) {
            if (ImGui::Selectable(type.c_str())) {
                newUIBlock(type, _sinkForNewPlot);
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Cancel", ImVec2(ImGui::GetWindowSize().x - (ImGui::GetStyle().WindowPadding.x * 2.f), 0))) {
            ImGui::CloseCurrentPopup();
        }
    }
}

void DashboardPage::setLayoutType(DockingLayoutType type) { _dockSpace.setLayoutType(type); }

} // namespace DigitizerUi
