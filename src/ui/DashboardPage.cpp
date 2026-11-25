#include "DashboardPage.hpp"

#include <exception>
#include <format>
#include <gnuradio-4.0/Logger.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <implot.h>
#include <memory>

#include "common/Events.hpp"
#include "common/Imw.hpp"
#include "common/LookAndFeel.hpp"

#include "components/Dialog.hpp"
#include "components/ImGuiNotify.hpp"
#include "components/Splitter.hpp"
#include "components/StyledWidgets.hpp"
#include "components/YesNoPopup.hpp"

#include "blocks/ImPlotSink.hpp"
#include "blocks/RemoteSource.hpp"
#include "charts/Chart.hpp"
#include "charts/SignalSink.hpp"
#include "charts/SinkRegistry.hpp"

namespace gr::od {

namespace {
bool plotSquareIconButton(const char* glyph, const char* tooltip, float buttonSize) noexcept {
    const bool ret = [&] {
        imw::StyleColor buttonStyle(ImGuiCol_Button, LookAndFeel::instance().palette().mainWindowButtonBgInactive);
        imw::StyleColor textStyle(ImGuiCol_Text, LookAndFeel::instance().palette().mainWindowButtonIcon.value_or(ImGui::GetStyleColorVec4(ImGuiCol_Text)));
        imw::StyleColor buttonActiveStyle(ImGuiCol_ButtonActive, LookAndFeel::instance().palette().mainWindowButtonBgActive);
        imw::StyleColor buttonHoveredStyle(ImGuiCol_ButtonHovered, LookAndFeel::instance().palette().mainWindowButtonBgHovered);
        imw::Font       font(LookAndFeel::instance().fontIconsSolidLarge);
        return ImGui::Button(glyph, {buttonSize, buttonSize});
    }();

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }

    return ret;
}
} // namespace

DashboardPage::DashboardPage() {
    charts::SinkRegistry::instance().addListener(this, [this, alive = std::weak_ptr(_alive)](SignalSink& sink, bool wasAdded) {
        if (!wasAdded) {
            return;
        }
        EventLoop::instance().executeLater([this, alive, signalName = std::string(sink.signalName()), sinkUniqueName = std::string(sink.uniqueName()), sinkName = std::string(sink.name())] {
            if (alive.expired() || !_dashboard) {
                return;
            }
            auto it = std::ranges::find_if(_addedSourceBlocksWaitingForSink, [&signalName](const auto& kvp) { return kvp.second.signalData.signalName == signalName; });
            if (it == _addedSourceBlocksWaitingForSink.end()) {
                return;
            }
            const std::string sourceBlockName = it->second.sourceBlockName;
            _addedSourceBlocksWaitingForSink.erase(it);

            _dashboard->session.sendToScheduler(gr::scheduler::property::kEmplaceEdge, emplaceEdgeData(sourceBlockName, "out", sinkUniqueName, "in"));

            _dashboard->newUIBlock("XYChart", {{"data_sinks", gr::Tensor<gr::pmt::Value>{sinkName}}});
        });
    });
}

DashboardPage::~DashboardPage() { charts::SinkRegistry::instance().removeListener(this); }

DashboardPage::PropertyControlWindowContextMenuAction DashboardPage::drawPropertyControlWindowContextMenu(const PropertyControlWindowsDrawParams& params) {
    auto action = PropertyControlWindowContextMenuAction::None;
    if (auto contextPopup = Popup(kPropertyControlWindowContextMenuId, PopupOptions{})) {
        namespace menu_icons = charts::menu_icons;
        if (menu_icons::menuItemWithIcon(menu_icons::kRemove, "Remove")) {
            params.removeList.push_back(params.windowId);
        }
        ImGui::SetItemTooltip("Removes the window");
        if (menu_icons::menuItemWithIcon(menu_icons::kFormat, "Change Label")) {
            this->_propertyControlWindowID = params.windowId;
            action                         = PropertyControlWindowContextMenuAction::OpenChangeLabelPopup;
        }
    }
    return action;
}

DashboardPage::PropertyControlWindowContextMenuAction DashboardPage::drawPropertyControlWindow(const PropertyControlWindowsDrawParams& params) {
    const auto windowRect       = ImGui::GetCurrentWindow()->WorkRect;
    const bool isHoveringWindow = ImGui::IsMouseHoveringRect(windowRect.Min, windowRect.Max);
    const bool isRightClicked   = ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    if (isHoveringWindow && isRightClicked) {
        ImGui::OpenPopup(kPropertyControlWindowContextMenuId);
    }

    return this->drawPropertyControlWindowContextMenu(params);
}

void DashboardPage::addPropertyControlWindows(const AddPropertyControlWindowsParams& params) {
    for (auto& [id, controlWindow] : _dashboard->propertyControlWindows) {
        params.output.emplace_back(controlWindow.window);

        auto*      existingWindow                      = ImGui::FindWindowByName(controlWindow.window->name.c_str());
        const bool docked                              = existingWindow ? existingWindow->DockIsActive : false;
        controlWindow.window->wantsHorizontalScrollbar = docked;

        auto  onContextMenuAction        = params.onContextMenuAction;
        auto& removeList                 = params.removeList;
        controlWindow.window->renderFunc = [this, id, onContextMenuAction, &removeList] {
            auto action = this->drawPropertyControlWindow({
                .windowId   = id,
                .removeList = removeList,
            });
            if (action != PropertyControlWindowContextMenuAction::None) {
                onContextMenuAction(action);
            }
        };
    }
}

void DashboardPage::drawChangeLabelPopup() {
    auto selectedWindowIter = _dashboard->propertyControlWindows.find(_propertyControlWindowID);
    if (selectedWindowIter == std::end(_dashboard->propertyControlWindows)) {
        return;
    }

    imw::StyleVar minSize(ImGuiStyleVar_WindowMinSize, ImVec2{LookAndFeel::instance().dashboardStyle.newChartPopupMinWidth, ImGui::GetFrameHeight()});
    if (auto popup = Popup(kChangeLabelPopupId, PopupOptions{.flags = PopupFlags::WantsExclusiveFocus | PopupFlags::Modal, .desktopWindowFlags = ImGuiWindowFlags_None})) {
        constexpr int kNumColumns = 2;
        if (auto table = imw::Table("labelTable", kNumColumns, ImGuiTableFlags_SizingFixedFit, ImVec2(0, 0), 0.0f)) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("label:");
            ImGui::TableSetColumnIndex(1);
            ImGui::InputText("##Change label", &selectedWindowIter->second.label);
        }
        std::ignore = components::dialogButtons();
    }
}

void DashboardPage::drawToolbarLayoutButtons(float plotButtonSize) noexcept {
    using enum DockingLayoutType;
    imw::Group layout;
    if (barControl(plotButtonSize, [&] { return plotSquareIconButton("\u{F7A5}", "change to the horizontal layout", plotButtonSize); })) {
        _dashboard->layoutType = Row;
    }
    ImGui::SameLine();
    if (barControl(plotButtonSize, [&] { return plotSquareIconButton("\u{F7A4}", "change to the vertical layout", plotButtonSize); })) {
        _dashboard->layoutType = Column;
    }
    ImGui::SameLine();
    if (barControl(plotButtonSize, [&] { return plotSquareIconButton("\u{F58D}", "change to the grid layout", plotButtonSize); })) {
        _dashboard->layoutType = Grid;
    }
    ImGui::SameLine();
    if (barControl(plotButtonSize, [&] { return plotSquareIconButton("\u{F248}", "change to the free layout", plotButtonSize); })) {
        _dashboard->layoutType = Free;
    }
    ImGui::SameLine();
}

void DashboardPage::addSelectedRemoteSignal(const SignalData& selectedRemoteSignal) noexcept {
    const auto& uri           = selectedRemoteSignal.uri();
    _addingRemoteSignals[uri] = selectedRemoteSignal;

    RemoteSources::instance().setRemoteSourceAddedCallback(uri, [this, alive = std::weak_ptr(_alive), selectedRemoteSignal](const RemoteSourceInfo& remoteSource) {
        EventLoop::instance().executeLater([this, alive, selectedRemoteSignal, sourceUniqueName = remoteSource.uniqueName, remoteSourceType = remoteSource.typeName] {
            if (!alive.expired()) {
                addSinkForRemoteSource(selectedRemoteSignal, sourceUniqueName, remoteSourceType);
            }
        });
    });
    _dashboard->addRemoteSignal(selectedRemoteSignal);
}

void DashboardPage::addSinkForRemoteSource(const SignalData& selectedRemoteSignal, const std::string& sourceUniqueName, const std::string& remoteSourceType) {
    if (!_dashboard) {
        return;
    }
    const auto& uriStr                       = selectedRemoteSignal.uri();
    _addedSourceBlocksWaitingForSink[uriStr] = SourceBlockInWaiting{
        .signalData      = std::move(_addingRemoteSignals[uriStr]), //
        .sourceBlockName = sourceUniqueName                         //
    };
    _addingRemoteSignals.erase(uriStr);

    const std::string_view sinkBlockType = remoteSourceType == gr::meta::type_name<RemoteStreamSource<float>>() ? registeredId<ImPlotSink<float>>() : remoteSourceType == gr::meta::type_name<RemoteStreamSource<double>>() ? registeredId<ImPlotSink<double>>() : remoteSourceType == gr::meta::type_name<RemoteDataSetSource<float>>() ? registeredId<ImPlotSink<gr::DataSet<float>>>() : remoteSourceType == gr::meta::type_name<RemoteDataSetSource<double>>() ? registeredId<ImPlotSink<gr::DataSet<double>>>() : withoutTemplateArguments(registeredId<ImPlotSink<float>>()); // not registered: the emplace fails
    // unique against both sink and signal names: data_sinks searches for either
    const auto          isSignalNameOfASink = [](std::string_view name) { return charts::SinkRegistry::instance().findSink([name](const auto& sink) { return sink.signalName() == name; }) != nullptr; };
    UniqueNameRegistry& nameRegistry        = _dashboard->session.graphModel.nameRegistry;
    std::string         sinkName            = nameRegistry.ensureNameIsUnique(std::format("{}_sink", registeredUniqueName(sourceUniqueName)));
    while (isSignalNameOfASink(sinkName)) {
        sinkName = nameRegistry.ensureNameIsUnique(sinkName);
    }
    gr::property_map sinkProperties{{"name", std::move(sinkName)}, {"signal_name", selectedRemoteSignal.signalName}};
    if (!selectedRemoteSignal.unit.empty()) {
        sinkProperties.emplace("signal_unit", selectedRemoteSignal.unit);
    }

    _dashboard->session.sendToScheduler(gr::scheduler::property::kEmplaceBlock, {{"type", std::string(sinkBlockType)}, {"properties", std::move(sinkProperties)}});
}

void DashboardPage::drawBarLeading(LegendItemClickResult& clickResult) noexcept {
    const float plotButtonSize = LookAndFeel::instance().mainWindowIconButtonSize();
    namespace dnd              = charts::dnd;
    if (barControl(plotButtonSize, [&] { return plotSquareIconButton("\u{F201}", "create new chart", plotButtonSize); })) {
        clickResult.shouldOpenNewPlotModal = true;
    }
    const bool dropped = dnd::acceptDrop(
        [&clickResult](const dnd::Payload& payload) {
            const auto sink = charts::SinkRegistry::instance().getSink(payload.sinkUniqueName);
            if (!sink) {
                return false;
            }
            std::string sinkName = charts::findSinkReference(*sink);
            if (sinkName.empty()) {
                return false;
            }
            clickResult.sinkForNewPlot = std::move(sinkName);
            return true;
        },
        dnd::kPayloadType);
    clickResult.shouldOpenNewPlotModal = clickResult.shouldOpenNewPlotModal || dropped;
    ImGui::SameLine();
}

void DashboardPage::openSignalSelector() noexcept {
    try {
        if (!_remoteSignalSelector) {
            _remoteSignalSelector = std::make_unique<SignalSelector>(_dashboard->session.graphModel);
        }
        _remoteSignalSelector->open();
    } catch (const std::exception& error) {
        components::Notification::error(std::format("Failed to open signal selector: {}", error.what()));
    }
}

std::vector<DashboardPage::BarButton> DashboardPage::barTrailingButtons(Mode mode, LegendItemClickResult& clickResult) {
    std::vector<BarButton> buttons;

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
    return buttons;
}

float DashboardPage::calculateMultipleIconButtonsWidth(std::size_t buttonCount) noexcept {
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    return spacing + (spacing + LookAndFeel::instance().mainWindowIconButtonSize()) * static_cast<float>(buttonCount);
}

void DashboardPage::drawBarTrailing(Mode mode, std::span<const BarButton> buttons) noexcept {
    const float plotButtonSize = LookAndFeel::instance().mainWindowIconButtonSize();

    if (mode == Mode::Interaction && _dashboard) {
        if (_remoteSignalSelector) {
            for (const auto& selectedRemoteSignal : _remoteSignalSelector->drawAndReturnSelected()) {
                this->addSelectedRemoteSignal(selectedRemoteSignal);
            }
        }
    }

    ImGui::SameLine();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    alignForWidth(calculateMultipleIconButtonsWidth(buttons.size()), 1.0);
    const float cursorBeforeButtons = ImGui::GetCursorPosX();

    for (const auto& button : buttons) {
        if (barControl(plotButtonSize, [&] { return plotSquareIconButton(button.icon, button.tooltip, plotButtonSize); })) {
            button.action();
        }
        ImGui::SameLine();
    }

    if (gAppState.prototypeMode) {
        ImGui::SameLine();
        const float fps     = ImGui::GetIO().Framerate;
        const auto  str     = std::format("FPS:{:5.0f}({:2}ms)", fps, gAppState.execTime.count());
        const auto  estSize = ImGui::CalcTextSize(str.c_str());
        ImGui::SetCursorPosX(cursorBeforeButtons - estSize.x - spacing);
        ImGui::Text("%s", str.c_str());
    }
}

DashboardPage::LegendItemClickResult DashboardPage::drawChartsLegendAndEditPane(Mode mode, std::vector<std::size_t>& windowRemoveList) {
    const float splitterWidth     = LookAndFeel::instance().splitterWidth;
    const float halfSplitterWidth = splitterWidth / 2.f;

    const float  left          = ImGui::GetCursorPosX();
    const float  top           = ImGui::GetCursorPosY();
    const ImVec2 screenTopLeft = ImGui::GetCursorScreenPos();
    const ImVec2 size          = ImGui::GetContentRegionAvail();

    const bool  horizontalSplit = size.x > size.y;
    const float ratio           = mode == Mode::Interaction ? components::splitter(_splitter, size, horizontalSplit, splitterWidth, LookAndFeel::instance().splitterDefaultRatio, !_editPane.selectedBlock()) : 0.f;

    ImGui::SetCursorPosX(left);
    ImGui::SetCursorPosY(top);

    LegendItemClickResult legendClickResult;
    auto                  contextMenuAction = PropertyControlWindowContextMenuAction::None;
    const float           plotButtonSize    = LookAndFeel::instance().mainWindowIconButtonSize();

    DashboardView::Options options;
    options.size = horizontalSplit ? ImVec2(size.x * (1.f - ratio) - halfSplitterWidth, size.y) : ImVec2(size.x, size.y * (1.f - ratio) - halfSplitterWidth);
    if (mode != Mode::View) {
        options.barLeading     = [&] { drawBarLeading(legendClickResult); };
        options.addDockWindows = [&](DockSpace::Windows& windows) {
            addPropertyControlWindows({
                .output              = windows,
                .onContextMenuAction = [&contextMenuAction](PropertyControlWindowContextMenuAction action) { contextMenuAction = action; },
                .removeList          = windowRemoveList,
            });
        };
    }
    if (mode == Mode::Layout) {
        options.barCentre = [&] { drawToolbarLayoutButtons(plotButtonSize); };
    }
    const std::vector<BarButton> trailingButtons = barTrailingButtons(mode, legendClickResult);
    options.barTrailing                          = [&] { drawBarTrailing(mode, trailingButtons); };
    options.barTrailingWidth                     = calculateMultipleIconButtonsWidth(trailingButtons.size());
    if (mode == Mode::Interaction) {
        options.addSignalButtonHidden = _barAutoHide.hidden;
    }

    _barHomeLastDrawn = std::exchange(_barHomeDrawing, kNoBarDrawn);
    if (mode == Mode::View) {
        _barAutoHide = {};
    } else {
        std::ignore = _barAutoHide.update(_barHomeLastDrawn);
    }
    const auto viewResult = _view.draw(*_dashboard, mode, options);
    if (viewResult.addSignalButtonRect.GetArea() > 0.f) {
        _barHomeDrawing.Add(viewResult.addSignalButtonRect);
    }
    if (viewResult.isAddSignalButtonPressed) {
        openSignalSelector();
    }
    if (mode == Mode::Interaction && _remoteSignalSelector) {
        for (const auto& selectedRemoteSignal : _remoteSignalSelector->drawAndReturnSelected()) {
            this->addSelectedRemoteSignal(selectedRemoteSignal);
        }
    }
    if (viewResult.backgroundClicked) {
        _editPane.setSelectedBlock(nullptr, nullptr);
    }
    if (!viewResult.rightClickedSinkName.empty()) {
        if (auto found = _dashboard->session.graphModel.recursiveFindBlockByUniqueName(viewResult.rightClickedSinkName)) {
            _editPane.setSelectedBlock(found.block, std::addressof(_dashboard->session.graphModel));
            _editPane.closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay;
        }
    }

    switch (contextMenuAction) {
    case PropertyControlWindowContextMenuAction::None: break;
    case PropertyControlWindowContextMenuAction::OpenChangeLabelPopup: ImGui::OpenPopup(kChangeLabelPopupId); break;
    }
    drawChangeLabelPopup();

    if (!legendClickResult.sinkForNewPlot.empty()) {
        _sinkForNewPlot = legendClickResult.sinkForNewPlot;
    }

    if (horizontalSplit) {
        const float w = size.x * ratio;
        components::blockControlsPanel(_editPane, {screenTopLeft.x + size.x - w + halfSplitterWidth, screenTopLeft.y}, {w - halfSplitterWidth, size.y}, true);
    } else {
        const float h = size.y * ratio;
        components::blockControlsPanel(_editPane, {screenTopLeft.x, screenTopLeft.y + size.y - h + halfSplitterWidth}, {size.x, h - halfSplitterWidth}, false);
    }

    return legendClickResult;
}

void DashboardPage::draw(Mode mode) noexcept {
    std::vector<std::size_t> windowRemoveList; // queue removal of windows here, submitted at end of draw()

    const auto legendClickResult = this->drawChartsLegendAndEditPane(mode, windowRemoveList);

    if (legendClickResult.shouldOpenNewPlotModal) {
        ImGui::OpenPopup(kAddChartPopupId);
    }
    drawNewPlotModal();

    if (legendClickResult.shouldOpenEnterViewOnlyModeModal) {
        ImGui::OpenPopup(kEnterViewOnlyModePopupId);
    }
    using namespace components;
    if (const auto popup = beginYesNoPopup(kEnterViewOnlyModePopupId, {.yesText = "Lock dashboard"}); isPopupOpen(popup)) {
        if (isPopupConfirmed(popup) && this->_requestViewOnlyMode) {
            this->_requestViewOnlyMode();
        }
        endPopup();
    }

    for (std::size_t id : windowRemoveList) {
        _dashboard->propertyControlWindows.erase(id);
    }
}

void DashboardPage::newUIBlock(std::string_view chartType, std::string_view initialSignal) {
    gr::property_map chartInitialParameters;
    if (!initialSignal.empty()) {
        chartInitialParameters["data_sinks"] = gr::Tensor<gr::pmt::Value>{initialSignal};
    }
    _dashboard->newUIBlock(chartType, chartInitialParameters);
}

void DashboardPage::drawNewPlotModal() {
    using namespace charts;

    const ImVec2 center = ImGui::GetWindowPos() + ImGui::GetWindowSize() * 0.5f;
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(LookAndFeel::instance().dashboardStyle.newChartPopupWidth, 0.f), ImGuiCond_Appearing);

    if (auto popup = Popup(kAddChartPopupId, PopupOptions{.flags = PopupFlags::WantsExclusiveFocus | PopupFlags::Modal, .desktopWindowFlags = ImGuiWindowFlags_AlwaysAutoResize})) {
        ImGui::Text("Select chart type:");
        ImGui::Separator();

        auto chartTypes = charts::registeredChartTypes();
        for (const auto& type : chartTypes) {
            if (selectable(type.c_str())) {
                newUIBlock(type, _sinkForNewPlot);
            }
        }

        ImGui::Separator();
        if (textButton("Cancel", ImVec2(ImGui::GetWindowSize().x - (ImGui::GetStyle().WindowPadding.x * 2.f), 0))) {
            closeCurrentPopup();
        }
    }
}

std::pair<DockingLayoutType, gr::property_map> DashboardPage::saveLayoutConfiguration() const {
    const gr::property_map& liveFreeLayout = _view.dockSpace().saveFreeLayout();
    if (_dashboard == nullptr) {
        return {_view.dockSpace().layoutType(), liveFreeLayout};
    }
    return {_dashboard->layoutType, liveFreeLayout.empty() ? _dashboard->windowLayout : liveFreeLayout};
}

} // namespace gr::od
