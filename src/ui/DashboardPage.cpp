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

struct PropertyInfo {
    const gr::pmt::Value&                        currentValue;
    const UiGraphBlock::SettingsMetaInformation& meta;
    UiGraphBlock&                                block;
};

static std::optional<PropertyInfo> getPropertyInfo(UiGraphModel& graphModel, const std::string& blockName, const std::string& propertyName) {
    if (auto* block = graphModel.recursiveFindBlockByName(blockName).block) {
        const auto propertyIter = block->blockSettings.find(propertyName);
        const auto metaIter     = block->blockSettingsMetaInformation.find(propertyName);
        if (propertyIter != std::end(block->blockSettings) && metaIter != std::end(block->blockSettingsMetaInformation)) {
            return PropertyInfo{propertyIter->second, metaIter->second, *block};
        }
    }
    return {};
};

static IMW::WidgetSize                                            //
getEditorWidgetSize(UiGraphModel&                     graphModel, //
    Dashboard::PropertyControlWindow&                 window,     //
    std::span<const components::ExportedPropertyPair> properties) //
{
    if (!properties.empty()) {
        const auto& [frontBlockName, frontPropertyName] = properties.front();
        if (const auto propertyInfo = getPropertyInfo(graphModel, frontBlockName, frontPropertyName)) {
            const auto& [currentValue, meta, _] = *propertyInfo;
            return components::calcEditorSize(window.label.c_str(), frontPropertyName, currentValue, meta);
        }
    }
    return {};
};

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

        auto& uiWindow = _dashboard->newUIBlock();
        auto  names    = grc_compat::getBlockSinkNames(uiWindow.block.get());
        names.push_back(sourceInWaiting.signalData.signalName);
        grc_compat::setBlockSinkNames(uiWindow.block.get(), names);
    });
}

DashboardPage::~DashboardPage() { opendigitizer::charts::SinkRegistry::instance().removeListener(this); }

DashboardPage::PropertyControlWindowContextMenuAction DashboardPage::drawPropertyControlWindowContextMenu(const PropertyControlWindowsDrawParams& params) {
    auto action = PropertyControlWindowContextMenuAction::None;
    if (auto contextPopup = IMW::Popup(propertyControLWindowContextWindowID, ImGuiPopupFlags_None)) {
        namespace menu_icons = opendigitizer::charts::menu_icons;
        if (menu_icons::menuItemWithIcon(menu_icons::kRemove, "Remove")) {
            params.removeList.push_back(params.windowId);
        }
        ImGui::SetItemTooltip("Removes the window and unbinds the assigned properties from each other");
        if (menu_icons::menuItemWithIcon(menu_icons::kDisconnect, "Disconnect")) {
            this->_propertyControlWindowID = params.windowId;
            action                         = PropertyControlWindowContextMenuAction::OpenDisconnectCurrentPropertiesPopup;
        }
        if (menu_icons::menuItemWithIcon(menu_icons::kFormat, "Change Label")) {
            this->_propertyControlWindowID = params.windowId;
            action                         = PropertyControlWindowContextMenuAction::OpenChangeLabelPopup;
        }
        ImGui::SetItemTooltip("Select properties to remove from this window's control");
    }
    return action;
}

DashboardPage::PropertyControlWindowContextMenuAction DashboardPage::drawPropertyControlWindow(const PropertyControlWindowsDrawParams& params) {
    this->propertyControlWindowEditProperties(params);

    // highlight drop target always even if there is no hovering from cursor, if the payload is compatible
    using ControlTypeAndBlock            = std::pair<UiGraphBlock::SettingsControlType, UiGraphBlock&>;
    const auto getControlTypeForProperty = [this](const components::ExportedPropertyPair& pair) -> std::optional<ControlTypeAndBlock> {
        if (auto propertyInfo = getPropertyInfo(this->_dashboard->graphModel, pair.blockName, pair.propertyName)) {
            const auto& [currentValue, meta, block] = *propertyInfo;
            return ControlTypeAndBlock{meta.controlType(pair.propertyName, currentValue), block};
        }
        return {};
    };
    const auto controlTypeConstraint = params.properties.empty() //
                                           ? std::optional<ControlTypeAndBlock>{}
                                           : getControlTypeForProperty(params.properties.front());

    const auto    draggedPropertyPair            = components::ExportedPropertyDragDropPayload::getCurrentPayload();
    bool          isValidDragPayloadActive       = false;
    UiGraphBlock* blockForDraggedPayloadProperty = nullptr;
    if (draggedPropertyPair) {
        if (auto maybeControlTypeAndProperty = getControlTypeForProperty(draggedPropertyPair)) {
            const auto& [draggedControlType, block] = *maybeControlTypeAndProperty;
            // property in the payload is valid, only now can we consider setting isValidDragPayloadActive to true
            isValidDragPayloadActive       = !controlTypeConstraint || controlTypeConstraint->first == draggedControlType;
            blockForDraggedPayloadProperty = std::addressof(block);
        }
    }

    const auto dragTargetRect   = ImGui::GetCurrentWindow()->WorkRect;
    const bool isHoveringTarget = ImGui::IsMouseHoveringRect(dragTargetRect.Min, dragTargetRect.Max);
    const bool isRightClicked   = ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    // always draw outline rect when user is dragging something
    if (draggedPropertyPair) {
        constexpr auto green     = rgbToImGuiABGR(0x28d14c);
        constexpr auto red       = rgbToImGuiABGR(0xf53333);
        const float    thickness = isHoveringTarget && isValidDragPayloadActive ? 5.f : 1.f;
        ImGui::GetCurrentWindow()->DrawList->AddRect(dragTargetRect.Min, dragTargetRect.Max, isValidDragPayloadActive ? green : red, 0, 0, thickness);
    }

    // install drop target over entire window
    if (ImGui::BeginDragDropTargetCustom(dragTargetRect, ImGui::GetID(std::format("{}##", params.controlWindow.window->name).c_str()))) {
        if (auto* accepted = ImGui::AcceptDragDropPayload(components::ExportedPropertyDragDropPayload::kType); isValidDragPayloadActive && accepted) {
            assert(blockForDraggedPayloadProperty);
            auto exportedIter = blockForDraggedPayloadProperty->exportedProperties.find(draggedPropertyPair.propertyName);
            if (exportedIter != std::end(blockForDraggedPayloadProperty->exportedProperties)) {
                exportedIter->second.windowId = params.windowId;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (isHoveringTarget && isRightClicked) {
        ImGui::OpenPopup(propertyControLWindowContextWindowID);
    }

    return this->drawPropertyControlWindowContextMenu(params);
}

void DashboardPage::propertyControlWindowEditProperties(const PropertyControlWindowsDrawParams& params) const {
    if (params.properties.empty()) {
        return;
    }

    const auto& [blockName, propertyName] = params.properties.front();

    const auto optionalPropertyInfo = getPropertyInfo(this->_dashboard->graphModel, blockName, propertyName);
    assert(optionalPropertyInfo && "property info should always be valid because its parameters were taken from the current graph");

    const auto& [currentValue, meta, block] = *optionalPropertyInfo;

    IMW::ChangeId id(static_cast<int>(params.windowId)); // push an ID in case the control label is empty and provides to differentiation

    ImGui::SetNextItemWidth(params.editWidgetSize.preferred.x - params.editWidgetSize.labelPreferredWidth);
    auto newPropertyValue = components::editBlockProperty(params.controlWindow.label.c_str(), propertyName, currentValue, meta);
    if (newPropertyValue) {
        block.setSetting(propertyName, std::move(newPropertyValue));
    }
}

DashboardPage::ExportedPropertyPairsByWindowID DashboardPage::getExportedPropertyPairsByWindowID() const noexcept {
    ExportedPropertyPairsByWindowID propertyPairsByWindowID;
    for (auto [blockName, exportedPropertiesPtr] : _dashboard->graphModel.recursiveGatherExportedProperties()) {
        for (auto& [propertyName, exportedPropertyInfo] : *exportedPropertiesPtr) {
            if (exportedPropertyInfo.windowId) {
                if (!_dashboard->propertyControlWindows.contains(*exportedPropertyInfo.windowId)) {
                    // window no longer exists
                    exportedPropertyInfo.windowId.reset();
                } else {
                    propertyPairsByWindowID.values[*exportedPropertyInfo.windowId].emplace_back(std::string{blockName}, propertyName);
                }
            }
        }
    }
    return propertyPairsByWindowID;
}

auto DashboardPage::ExportedPropertyPairsByWindowID::getForWindow(std::size_t id) const noexcept -> PropertyPairSpan {
    const auto propertyPairsIter = values.find(id);
    return propertyPairsIter == std::end(values) ? PropertyPairSpan{} : propertyPairsIter->second;
}

void DashboardPage::addPropertyControlWindows(const AddPropertyControlWindowsParams& params) {
    for (auto& [id, controlWindow] : _dashboard->propertyControlWindows) {
        params.output.emplace_back(controlWindow.window);

        auto propertyPairsForThisWindow = params.pairs.getForWindow(id);

        const IMW::WidgetSize editorWidgetSize     = getEditorWidgetSize(_dashboard->graphModel, controlWindow, propertyPairsForThisWindow);
        const auto            windowTitleBarHeight = ImGui::GetFrameHeight();
        const auto&           style                = ImGui::GetStyle();
        const ImVec2          fittedSize{
            editorWidgetSize.preferred.x + style.WindowPadding.x * 2.0f,
            editorWidgetSize.preferred.y + style.WindowPadding.y * 2.0f + windowTitleBarHeight,
        };

        if (!propertyPairsForThisWindow.empty()) {
            controlWindow.window->windowMinSizeOverride = fittedSize;
            controlWindow.window->windowMaxSize         = fittedSize;
        } else {
            controlWindow.window->windowMinSizeOverride.reset();
            controlWindow.window->windowMaxSize.reset();
        }

        auto*      existingWindow                      = ImGui::FindWindowByName(controlWindow.window->name.c_str());
        const bool docked                              = existingWindow ? existingWindow->DockIsActive : false;
        controlWindow.window->wantsHorizontalScrollbar = docked;

        auto  onContextMenuAction        = params.onContextMenuAction;
        auto& removeList                 = params.removeList;
        controlWindow.window->renderFunc = [this, id, &controlWindow, onContextMenuAction, &removeList, editorWidgetSize, propertyPairsForThisWindow] {
            auto action = this->drawPropertyControlWindow({
                .windowId       = id,
                .controlWindow  = controlWindow,
                .properties     = propertyPairsForThisWindow,
                .editWidgetSize = editorWidgetSize,
                .removeList     = removeList,
            });
            if (action != PropertyControlWindowContextMenuAction::None) {
                onContextMenuAction(action);
            }
        };
    }
}

void DashboardPage::drawCurrentPropertiesPopup(const ExportedPropertyPairsByWindowID& pairs) {
    bool          isOpen = true;
    IMW::StyleVar minSize(ImGuiStyleVar_WindowMinSize, ImVec2{300.f, 200.f});
    auto          popup = IMW::ModalPopup(currentPropertiesPopupID, &isOpen, 0);
    if (!popup) {
        return;
    }

    {
        IMW::Font             font(LookAndFeel::instance().fontBigger[LookAndFeel::instance().prototypeMode]);
        constexpr const char* title = "Disconnect Properties";
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(title).x) / 2.f);
        ImGui::TextUnformatted(title);
    }

    IMW::Child child("childProperties", ImVec2{}, 0, 0);

    const std::size_t numColumns = 3;
    if (auto table = IMW::Table("propertiesTable", numColumns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg, ImVec2(0, 0), 0.0f)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

        std::size_t id = 0;
        for (const auto& [blockName, propertyName] : pairs.getForWindow(_propertyControlWindowID)) {
            IMW::ChangeId rowId{++id};
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            const auto propertyInfo = getPropertyInfo(_dashboard->graphModel, blockName, propertyName);
            if (!propertyInfo) {
                assert(false && "Attempt to edit nonexistent property or block");
                continue;
            }
            const auto& [currentValue, meta, block] = *propertyInfo;

            ImGui::TableSetColumnIndex(0);

            constexpr ImGuiSelectableFlags flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
            const ImVec2                   selectableDimensions{0, ImGui::GetFrameHeight()};
            const bool                     clicked = ImGui::Selectable(std::format("##selectable{}{}", blockName, propertyName).c_str(), false, flags, selectableDimensions);

            ImGui::SameLine();
            const auto controlType = meta.controlType(propertyName, currentValue);
            components::ExportedPropertyList::drawPropertyUninteractiveNoLabel(propertyName.c_str(), controlType, currentValue);

            ImGui::TableSetColumnIndex(1);

            ImGui::TextUnformatted(block.blockName.c_str());

            ImGui::TableSetColumnIndex(2);

            ImGui::TextUnformatted(propertyName.c_str());

            if (clicked) {
                block.exportedProperties.at(propertyName).windowId.reset();
            }
        }
    }
}

void DashboardPage::drawChangeLabelPopup() {
    auto selectedWindowIter = _dashboard->propertyControlWindows.find(_propertyControlWindowID);
    if (selectedWindowIter == std::end(_dashboard->propertyControlWindows)) {
        return;
    }

    bool          isOpen = true;
    IMW::StyleVar minSize(ImGuiStyleVar_WindowMinSize, ImVec2{200.f, ImGui::GetFrameHeight()});
    if (auto popup = IMW::ModalPopup(changeLabelPopupID, &isOpen, ImGuiPopupFlags_None)) {
        constexpr int numColumns = 2;
        if (auto table = IMW::Table("labelTable", numColumns, ImGuiTableFlags_SizingFixedFit, ImVec2(0, 0), 0.0f)) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("label:");
            ImGui::TableSetColumnIndex(1);
            ImGui::InputText("##Change label", &selectedWindowIter->second.label);
        }
    }
}

ImVec2 DashboardPage::drawCharts(Mode mode, const ExportedPropertyPairsByWindowID& propertyPairsByWindowID, std::vector<std::size_t>& windowRemoveList) {
    IMW::Group group;

    ImVec2 paneSize = ImGui::GetContentRegionAvail();
    paneSize.y -= _legendBox.y;

    const float w = paneSize.x / float(kGridWidth);
    // const float h = paneSize.y / float(kGridHeight);

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
            y += w; // TODO maybe should be h here?
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
        uiWindow.window->renderFunc = [block = blockPtr, mode] {
            gr::property_map drawConfig;
            drawConfig["chartMode"] = magic_enum::enum_name(mode);
            std::ignore = block->draw(drawConfig);
        };

        uiWindow.window->renderDockingContextMenuFunc = [block = blockPtr] {
            opendigitizer::charts::drawDuplicateChartMenuItem(block->uniqueName());
            opendigitizer::charts::drawRemoveChartMenuItem(block->uniqueName());
        };
    }

    // out vars, not set until after .render() and window callbacks below
    auto contextMenuAction = PropertyControlWindowContextMenuAction::None;

    this->addPropertyControlWindows({
        .output              = windows,
        .pairs               = propertyPairsByWindowID,
        .onContextMenuAction = [&contextMenuAction](PropertyControlWindowContextMenuAction action) { contextMenuAction = action; },
        .removeList          = windowRemoveList,
    });

    _dockSpace.render(windows, paneSize, mode == Mode::Layout);

    // now that render() has called, contextMenuAction has been populated by callback
    {
        using enum PropertyControlWindowContextMenuAction;
        switch (contextMenuAction) {
        case None: break;
        case OpenChangeLabelPopup: ImGui::OpenPopup(changeLabelPopupID); break;
        case OpenDisconnectCurrentPropertiesPopup: ImGui::OpenPopup(currentPropertiesPopupID); break;
        }
    }

    this->drawCurrentPropertiesPopup(propertyPairsByWindowID);
    this->drawChangeLabelPopup();

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
    _signalLegend.setDragDropEnabled(mode == Mode::Interaction);
    auto rightClickedSinkName = _signalLegend.draw(_dashboard->graphModel, chartPaneSize.x);
    if (mode != Mode::Layout && !rightClickedSinkName.empty()) {
        if (auto found = _dashboard->graphModel.recursiveFindBlockByUniqueName(std::string(rightClickedSinkName))) {
            _editPane.setSelectedBlock(found.block, std::addressof(_dashboard->graphModel));
            _editPane.closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay;
        }
    }
    return _signalLegend.legendSize();
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

void DashboardPage::applyControlPanelWindowAction(const components::BlockControlsPanelResult& controlPanelAction, const ExportedPropertyPairsByWindowID& pairs, std::vector<std::size_t>& windowRemoveList) {
    if (controlPanelAction.allExportedPropertiesPageResult) {
        using Action         = components::ExportedPropertyList::Action;
        const auto& property = controlPanelAction.allExportedPropertiesPageResult.targetProperty;
        switch (controlPanelAction.allExportedPropertiesPageResult.action) {
        case Action::Unexport: {
            if (const auto propertyInfo = getPropertyInfo(_dashboard->graphModel, property.blockName, property.propertyName)) {
                propertyInfo->block.exportedProperties.erase(property.propertyName);
            }
            break;
        }
        case Action::AddWindow: {
            if (const auto propertyInfo = getPropertyInfo(_dashboard->graphModel, property.blockName, property.propertyName)) {
                const auto [currentValue, meta, block] = *propertyInfo;
                // if we got here, the property and block must actually exist, so it's okay to make a window now
                const auto& [windowId, _] = _dashboard->newPropertyControlWindow(std::addressof(block), property.propertyName, property.propertyName);

                propertyInfo->block.exportedProperties.find(property.propertyName)->second.windowId = windowId;
            }
            break;
        }
        case Action::Selected: break;
        }
    }
    if (controlPanelAction.blockEditPaneResult) {
        const auto& [type, block, propertyName] = controlPanelAction.blockEditPaneResult;
        using enum components::BlockPropertyEditResult::Type;
        switch (type) {
        case AddNewWindow: {
            const auto& [id, window]                         = this->_dashboard->newPropertyControlWindow(block, propertyName, propertyName);
            block->exportedProperties[propertyName].windowId = id;
            window.window->wantsDockAtBottom                 = true; // dockspace relayout() is about to be triggered due to a new window, it will see this
            ImGui::FocusWindow(ImGui::FindWindowByName(window.window->name.c_str()));
        } break;
        case RemoveFromExistingWindow: {
            auto exportedIter = block->exportedProperties.find(propertyName);
            if (exportedIter != std::end(block->exportedProperties) && exportedIter->second.windowId.has_value()) {
                // erase window if this property being removed was the last one
                auto pairsForExistingWindow = pairs.getForWindow(*exportedIter->second.windowId);
                if (pairsForExistingWindow.size() == 1) {
                    assert(pairsForExistingWindow.front().blockName == block->blockName);
                    assert(pairsForExistingWindow.front().propertyName == propertyName);
                    windowRemoveList.push_back(*exportedIter->second.windowId);
                }
                exportedIter->second.windowId.reset();
            }
        } break;
        }
    }
}

DashboardPage::LegendItemClickResult DashboardPage::drawChartsLegendAndEditPane(Mode mode, const ExportedPropertyPairsByWindowID& propertyPairsByWindowID, std::vector<std::size_t>& windowRemoveList) {
    constexpr float splitterWidth     = 6;
    constexpr float halfSplitterWidth = splitterWidth / 2.f;

    const float  left = ImGui::GetCursorPosX();
    const float  top  = ImGui::GetCursorPosY();
    const ImVec2 size = ImGui::GetContentRegionAvail();

    const bool  horizontalSplit = size.x > size.y;
    const float ratio           = mode == Mode::Interaction ? components::Splitter(size, horizontalSplit, splitterWidth, 0.2f, !_editPane.selectedBlock()) : 0.f;

    ImGui::SetCursorPosX(left);
    ImGui::SetCursorPosY(top);

    LegendItemClickResult legendClickResult;
    IMW::Child            plotsChild("##plots", horizontalSplit ? ImVec2(size.x * (1.f - ratio) - halfSplitterWidth, size.y) : ImVec2(size.x, size.y * (1.f - ratio) - halfSplitterWidth), false, ImGuiWindowFlags_NoScrollbar);

    if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        _editPane.setSelectedBlock(nullptr, nullptr);
    }

    // chart
    const auto paneSize = this->drawCharts(mode, propertyPairsByWindowID, windowRemoveList);
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

    // legend
    legendClickResult = this->drawLegend(mode, paneSize);

    if (!legendClickResult.sinkForNewPlot.empty()) {
        _sinkForNewPlot = legendClickResult.sinkForNewPlot;
    }

    // edit pane
    if (horizontalSplit) {
        const float w = size.x * ratio;
        applyControlPanelWindowAction(components::BlockControlsPanel(_editPane, {left + size.x - w + halfSplitterWidth, top}, {w - halfSplitterWidth, size.y}, true), propertyPairsByWindowID, windowRemoveList);
    } else {
        const float h = size.y * ratio;
        applyControlPanelWindowAction(components::BlockControlsPanel(_editPane, {left, top + size.y - h + halfSplitterWidth}, {size.x, h - halfSplitterWidth}, false), propertyPairsByWindowID, windowRemoveList);
    }

    return legendClickResult;
}

void DashboardPage::draw(Mode mode) noexcept {
    processPendingTransmutation();
    processPendingRemovals();

    const auto               propertyPairsByWindowID = this->getExportedPropertyPairsByWindowID();
    std::vector<std::size_t> windowRemoveList; // queue removal of windows here, submitted at end of draw()

    const auto legendClickResult = this->drawChartsLegendAndEditPane(mode, propertyPairsByWindowID, windowRemoveList);

    // Modal dialogs
    if (legendClickResult.shouldOpenNewPlotModal) {
        ImGui::OpenPopup(addChartPopupID);
    }
    drawNewPlotModal();

    if (legendClickResult.shouldOpenEnterViewOnlyModeModal) {
        ImGui::OpenPopup(enterViewOnlyModePopupID);
    }
    using namespace components;
    if (const auto popup = beginYesNoPopup(enterViewOnlyModePopupID, {.yesText = "Lock dashboard"}); isPopupOpen(popup)) {
        if (isPopupConfirmed(popup) && this->_requestViewOnlyMode) {
            this->_requestViewOnlyMode();
        }
        ImGui::EndPopup();
    }

    // submit window remove list
    if (!windowRemoveList.empty()) {
        auto allExportedProperties = _dashboard->graphModel.recursiveGatherExportedProperties();
        for (std::size_t id : windowRemoveList) {
            // unbind properties
            for (auto& [_, exportedPropertiesPtr] : allExportedProperties) {
                for (auto& [propertyName, exportedPropertyInfo] : *exportedPropertiesPtr) {
                    if (exportedPropertyInfo.windowId == id) {
                        exportedPropertyInfo.windowId.reset();
                    }
                }
            }
            // remove ui windows
            _dashboard->propertyControlWindows.erase(id);
        }
    }
}

DigitizerUi::Dashboard::UIWindow* DashboardPage::newUIBlock(std::string_view chartType, std::string_view initialSignal) {
    if (_dashboard->uiWindows.size() < kMaxPlots) {
        gr::property_map chartInitialParameters;
        if (!initialSignal.empty()) {
            chartInitialParameters["data_sinks"] = gr::Tensor<gr::pmt::Value>{initialSignal};
        }
        return std::addressof(_dashboard->newUIBlock(chartType, chartInitialParameters));
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

void DashboardPage::setLayoutConfiguration(DockingLayoutType type, std::optional<gr::property_map> freeLayoutDescription) {
    _dockSpace.setLayoutType(type);
    if (freeLayoutDescription) {
        _dockSpace.loadFreeLayout(*freeLayoutDescription);
    }
}

std::pair<DockingLayoutType, gr::property_map> DashboardPage::saveLayoutConfiguration() const { return {_dockSpace.layoutType(), _dockSpace.saveFreeLayout()}; }

} // namespace DigitizerUi
