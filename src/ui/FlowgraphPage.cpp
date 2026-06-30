#include "FlowgraphPage.hpp"

#include <algorithm>

#include <crude_json.h>
#include <cstdint>
#include <format>

#include <gnuradio-4.0/PmtTypeHelpers.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include "GraphModel.hpp"
#include "common/ImguiWrap.hpp"

#include <imgui.h>
#include <imgui_node_editor.h>
#include <misc/cpp/imgui_stdlib.h>

#include "common/LookAndFeel.hpp"

#include "components/Splitter.hpp"
#include "components/YesNoPopup.hpp"

#include "utils/TransparentStringHash.hpp"

#include "scope_exit.hpp"

using namespace std::string_literals;

namespace {
bool isPortConnected(const DigitizerUi::UiGraphPort& port, const std::vector<DigitizerUi::UiGraphEdge>& edges) {
    return std::ranges::any_of(edges, [&port](const auto& edge) { //
        return edge.edgeSourcePort == &port || edge.edgeDestinationPort == &port;
    });
}
} // namespace

namespace DigitizerUi {

uint32_t darkenOrLighten(uint32_t color) {
    if (LookAndFeel::instance().style == LookAndFeel::Style::Light) {
        uint32_t r = color & 0xff000000;
        for (int i = 0; i < 3; ++i) {
            int shift = 8 * i;
            r |= uint32_t(((color >> shift) & 0xff) * 0.5) << shift;
        }
        return r;
    } else {
        uint32_t r = color & 0xff000000;
        for (int i = 0; i < 3; ++i) {
            int      shift   = 8 * i;
            uint32_t channel = (color >> shift) & 0xff;
            channel          = 0xff - ((0xff - channel) / 2);
            r |= channel << shift;
        }
        return r;
    }
}

auto topologicalSort(const std::vector<std::unique_ptr<UiGraphBlock>>& blocks, const std::vector<UiGraphEdge>& edges) {
    struct SortLevel {
        std::vector<const UiGraphBlock*> blocks;
    };

    struct BlockConnections {
        std::unordered_set<const UiGraphBlock*> parents;
        std::unordered_set<const UiGraphBlock*> children;
    };

    std::unordered_map<const UiGraphBlock*, BlockConnections> graphConnections;
    std::vector<SortLevel>                                    result;

    for (const auto& block : blocks) {
        graphConnections[block.get()];
    }

    for (const auto& edge : edges) {
        graphConnections[edge.edgeSourcePort->ownerBlock].children.insert(edge.edgeDestinationPort->ownerBlock);
        graphConnections[edge.edgeDestinationPort->ownerBlock].parents.insert(edge.edgeSourcePort->ownerBlock);
    }

    while (!graphConnections.empty()) {
        SortLevel newLevel;
        for (const auto& [block, connections] : graphConnections) {
            if (connections.parents.empty()) {
                newLevel.blocks.push_back(block);
            }
        }

        for (const auto* block : newLevel.blocks) {
            graphConnections.erase(block);
            for (auto& [_, connections] : graphConnections) {
                connections.parents.erase(block);
                // TODO(NOW) Proper top sort would use this to initialize the next level blocks
            }
        }

        if (newLevel.blocks.empty()) {
            break;
        }

        std::ranges::reverse(newLevel.blocks);

        result.push_back(std::move(newLevel));
    }

    // If there are blocks in graphConnections, we have at lease one cycle,
    // those blocks will not be sorted. Put them in the last level.
    if (!graphConnections.empty()) {
        SortLevel newLevel;
        std::ranges::transform(graphConnections, std::back_inserter(newLevel.blocks), [](const auto& kvp) { return kvp.first; });
    }

    return result;
}

float pinLocalPositionY(std::size_t index, std::size_t numPins, float blockHeight, float pinHeight) {
    const float spacing = blockHeight / (static_cast<float>(numPins) + 1);
    // ImFloor here is to mimic what imgui node editor is doing internally, so our rectangles line up with the highlight rects they draw
    return ImFloor(spacing * (static_cast<float>(index) + 1) - (pinHeight / 2));
}

void addPin(ax::NodeEditor::PinId id, ax::NodeEditor::PinKind kind, const ImVec2& p, ImVec2 size) {
    const bool   input = kind == ax::NodeEditor::PinKind::Input;
    const ImVec2 min   = input ? p - ImVec2(size.x, 0) : p;
    const ImVec2 max   = input ? p + ImVec2(0, size.y) : p + size;
    const ImVec2 pivot = ImVec2(input ? min.x : max.x, (min.y + max.y) / 2.f);

    if (input) {
        ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PinArrowSize, 10);
        ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PinArrowWidth, 10);
        ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_SnapLinkToPinDir, 1);
    }

    ax::NodeEditor::BeginPin(id, kind);
    ax::NodeEditor::PinPivotRect(pivot, pivot);
    ax::NodeEditor::PinRect(min, max);
    ax::NodeEditor::EndPin();

    if (input) {
        ax::NodeEditor::PopStyleVar(3);
    }
};

void drawPin(ImDrawList* drawList, ImVec2 pinPosition, gr::PortDirection pinDirection, ImVec2 pinSize, const std::string& name, const std::string& type, bool mainFlowGraph, bool isSupressed) {
    const auto& style = FlowgraphPage::styleForDataType(type);

    std::uint32_t alphaClearMask = 0x00ffffff;
    std::uint32_t alphaSetMask   = 0xff000000;
    if (ImGui::GetStyle().Alpha < 0.9f) {
        alphaSetMask = static_cast<std::uint32_t>(ImGui::GetStyle().Alpha * 255);
        alphaSetMask <<= 3 * 8;
    }

    const auto color       = (style.color & alphaClearMask) | alphaSetMask;
    const auto borderColor = darkenOrLighten(color);

    if (isSupressed) {
        // draw normal pin except it is extended with rounded corners, like a tab
        const float       iconTextSize = std::min(5.f, pinSize.y - 4.f);
        const float       tabWidth     = iconTextSize; // tab width is the whole icon size, though it is drawn only halfway poking out (tabCenterX)
        const float       radius       = pinSize.y / 2.f;
        const bool        isInput      = pinDirection == gr::PortDirection::INPUT;
        const ImDrawFlags roundFlags   = isInput ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersLeft;
        const ImVec2      pinMin{isInput ? pinPosition.x : pinPosition.x - tabWidth, pinPosition.y};
        const ImVec2      pinMax{isInput ? pinPosition.x + pinSize.x + tabWidth : pinPosition.x + pinSize.x, pinPosition.y + pinSize.y};
        drawList->AddRectFilled(pinMin, pinMax, color, radius, roundFlags);
        drawList->AddRect(pinMin, pinMax, borderColor, radius, roundFlags);
        const auto iconText   = "\u{f132}";
        const auto changeFont = IMW::FontWithSize{LookAndFeel::instance().fontIconsSolid, iconTextSize};
        // draw shield icon within the extended part of the pin
        const auto   middle     = pinPosition.y + (pinSize.y / 2.f);
        const float  tabCenterX = isInput ? pinPosition.x + pinSize.x : pinPosition.x;
        const ImVec2 iconSize   = ImGui::CalcTextSize(iconText);
        drawList->AddText(ImVec2{tabCenterX - (iconSize.x / 2.f), middle - (iconSize.y / 2.f)}, borderColor, iconText);
    } else {
        drawList->AddRectFilled(pinPosition, pinPosition + pinSize, color);
        drawList->AddRect(pinPosition, pinPosition + pinSize, borderColor);
    }

    ImGui::SetCursorPos(pinPosition);

    if (ImGui::IsMouseHoveringRect(pinPosition, pinPosition + pinSize)) {
        // Node editor has very limited support for tooltips.
        // See imgui-node-editor/examples/widgets-example/widgets-example.cpp for workarounds
        // such as this one:

        if (mainFlowGraph) {
            ax::NodeEditor::Suspend();
        }

        ImGui::SetTooltip("%s (%s)", name.c_str(), type.c_str());

        if (mainFlowGraph) {
            ax::NodeEditor::Resume();
        }
    }
};

std::string getDefaultExportedName(const UiGraphPort* port) { return std::format("{}.{}", port->ownerBlock ? port->ownerBlock->blockName : "UNKNOWN", port->portName); }

struct PinDrawInfo {
    ImVec2  topLeft;
    ImVec2  size;
    ImFont* font     = nullptr;
    float   fontSize = 0;
};

std::optional<std::string> exportedPortShortenedDisplayName(const UiGraphPort* port, const UiGraphBlock* exportedTo) {
    auto exportedName = port->getExportedName(exportedTo);

    if (!exportedName) {
        return exportedName;
    }

    std::string name;
    if (exportedName == getDefaultExportedName(port)) {
        name = port->portName;
    } else {
        name = *exportedName;
    }

    constexpr std::size_t maxDisplayNameLength = 10;
    if (name.size() > maxDisplayNameLength) {
        name = "..." + name.substr(name.size() - maxDisplayNameLength);
    }
    return name;
}

PinDrawInfo calculatePinDrawInfo(const std::optional<std::string>& displayName, std::size_t index, std::size_t numPins, float anchorX, float blockTopY, float blockHeight, bool isInput) {
    const auto& fg = LookAndFeel::instance().flowgraph;

    if (!displayName) {
        const float y = blockTopY + pinLocalPositionY(index, numPins, blockHeight, fg.pinHeight);
        const float x = isInput ? (anchorX - fg.pinHeight) : anchorX;
        return {.topLeft = {x, y}, .size = {fg.pinWidth, fg.pinHeight}};
    }

    auto* font = LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode];
    if (!font) {
        font = ImGui::GetFont();
    }
    const float fontSize = font->LegacySize;
    const auto  textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, displayName->c_str());
    const float tabW     = textSize.x + fg.exportedTabPaddingH * 2;
    const float tabH     = textSize.y + fg.exportedTabPaddingV * 2;
    const float tabY     = blockTopY + pinLocalPositionY(index, numPins, blockHeight, tabH);
    const float tabX     = isInput ? (anchorX + fg.exportedTabOverlap - tabW) : (anchorX - fg.exportedTabOverlap);

    return {.topLeft = {tabX, tabY}, .size = {tabW, tabH}, .font = font, .fontSize = fontSize};
}

std::string valToString(const gr::pmt::Value& val) {
    std::string out;
    gr::pmt::ValueVisitor([&]<typename TArg>(const TArg& arg) {
        using T = std::decay_t<TArg>;
        if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view> || std::same_as<T, std::pmr::string>) {
            out = std::string(arg);
        } else if constexpr (std::same_as<T, bool>) {
            out = arg ? "true" : "false";
        } else if constexpr (std::integral<T> || std::floating_point<T>) {
            out = std::to_string(arg);
        } else {
            out.clear();
        }
    }).visit(val);
    return out;
}

enum class ExportedState {
    UnexportedAndSupressed,
    Unexported,
    Exported,
};

std::string displayNameForExportedState(const UiGraphPort& port, ExportedState state) {
    switch (state) {
    case ExportedState::UnexportedAndSupressed: return std::format("Unexported##{}-{}", port.ownerBlock->blockUniqueName, port.portName);
    case ExportedState::Unexported: return std::format("Auto-export on close##{}-{}", port.ownerBlock->blockUniqueName, port.portName);
    case ExportedState::Exported: return std::format("Exported##{}-{}", port.ownerBlock->blockUniqueName, port.portName);
    }
    std::unreachable();
};

void FlowgraphEditor::drawPortExportOptionsMenu(const UiGraphPort& port, const char* portDirection) {
    const auto shortedExportedName = exportedPortShortenedDisplayName(&port, this->_exportPortTargetBlock);
    const bool suppressed          = port.isAutoExportSuppressed();
    const bool exported            = shortedExportedName.has_value();

    const auto exportedState = [suppressed, exported] {
        if (suppressed) {
            return ExportedState::UnexportedAndSupressed;
        } else {
            return exported ? ExportedState::Exported : ExportedState::Unexported;
        }
    }();

    const auto fromSuppressed = [&port] { port.ownerBlock->setPortAutoExportSuppressed(port, false); };
    const auto fromExported   = [this, &portDirection, &port, exportedState](bool suppressAfter) {
        if (exportedState != ExportedState::Exported) {
            return;
        }
        ExportPortMessageData unexportMessage{
              .uniqueBlockName = _selectedBlock->blockUniqueName,
              .portDirection   = portDirection,
              .portName        = port.portName,
              .exportedName    = "", // -Wmissing-designated-field-initializers
              .exportFlag      = false,
        };
        const auto exportedName = port.getExportedName(_exportPortTargetBlock);
        if (exportedName && hasExternalEdgesForExportedPort(*exportedName)) {
            // defer to a confirmation popup, external edges would be disconnected
            unexportPortRequest = UnexportPortRequest{.message = std::move(unexportMessage), .exportedName = *exportedName, .suppressAfter = suppressAfter};
            return;
        }
        requestExportPort(unexportMessage);
        if (suppressAfter) {
            port.ownerBlock->setPortAutoExportSuppressed(port, true);
        }
    };

    if (ImGui::RadioButton(displayNameForExportedState(port, ExportedState::UnexportedAndSupressed).c_str(), exportedState == ExportedState::UnexportedAndSupressed)) {
        if (exportedState == ExportedState::Exported) {
            fromExported(/*suppressAfter=*/true);
        } else {
            port.ownerBlock->setPortAutoExportSuppressed(port, true);
        }
    }
    if (ImGui::RadioButton(displayNameForExportedState(port, ExportedState::Exported).c_str(), exportedState == ExportedState::Exported)) {
        fromSuppressed();
        exportPortTextField = !exported ? getDefaultExportedName(&port) : std::string{};
        exportPortRequest   = ExportPortMessageData{
              .uniqueBlockName = _selectedBlock->blockUniqueName,
              .portDirection   = portDirection,
              .portName        = port.portName,
              .exportedName    = "", // -Wmissing-designated-field-initializers
              .exportFlag      = true,
        };
    }
    if (ImGui::RadioButton(displayNameForExportedState(port, ExportedState::Unexported).c_str(), exportedState == ExportedState::Unexported)) {
        fromSuppressed();
        fromExported(/*suppressAfter=*/false);
    }
}

FlowgraphEditor::Buttons FlowgraphEditor::drawButtons(const ImVec2& contentTopLeft, const ImVec2& contentSize, Buttons buttons, float horizontalSplitRatio) {
    Buttons result;

    IMW::PushCursorPosition _;

    static constexpr float padding = 16.0f;
    static constexpr float height  = 37.0f;

    {
        ImGui::SetNextWindowPos({contentTopLeft.x, contentTopLeft.y + contentSize.y - height - padding});
        ImGui::SetNextWindowSize({contentSize.x * (1 - horizontalSplitRatio), height});
        IMW::Window overlay("Button Overlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

        // These Buttons are rendered on top of the Editor, to make them properly readable, take out the transparency
        ImVec4 buttonColor = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        buttonColor.w      = 1.0f;

        {
            IMW::StyleColor buttonStyle(ImGuiCol_Button, buttonColor);

            ImGui::SetCursorPosX(padding);

            if (buttons.openNewBlockDialog) {
                if (ImGui::Button("Add block...")) {
                    result.openNewBlockDialog = true;
                }
                ImGui::SameLine();
            }

            if (buttons.openNewSubGraphDialog) {
                if (ImGui::Button("Add sub graph...")) {
                    result.openNewSubGraphDialog = true;
                }
                ImGui::SameLine();
            }

            if (buttons.openRemoteSignalSelector) {
                if (ImGui::Button("Add remote signal...")) {
                    result.openRemoteSignalSelector = true;
                }
                ImGui::SameLine();
            }

            auto placeButtonRight = [posFromRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - padding](const char* text) mutable { //
                float width = ImGui::CalcTextSize(text).x;

                ImGui::SetCursorPosX(posFromRight - width);
                posFromRight -= width + padding;

                bool clicked = false;
                if (ImGui::Button(text)) {
                    clicked = true;
                }
                ImGui::SameLine();

                return clicked;
            };

            if (buttons.closeWindow) {
                result.closeWindow = placeButtonRight("Close");
            }

            if (buttons.rearrangeBlocks) {
                result.rearrangeBlocks = placeButtonRight("Rearrange blocks");
            }
        }
    }

    return result;
}

void FlowgraphEditor::drawComputeDomainTag(UiGraphBlock& block) {
    if (!block.ownerGraph) {
        assert(false && "scheduler {} had no owner graph");
        return;
    }
    std::string tagLabel = "Unmanaged graph";
    if (block.isScheduler()) {
        auto computeDomainIter = block.blockSettings.find("compute_domain");
        if (computeDomainIter == std::end(block.blockSettings)) {
            return;
        }
        auto computeDomain = gr::ComputeDomain::parse(computeDomainIter->second.value_or(std::string_view{}));
        if (computeDomain.kind.empty()) {
            return;
        }
        tagLabel = computeDomain.kind;
    }

    const auto blockId         = ax::NodeEditor::NodeId(std::addressof(block));
    const auto nodePosition    = ax::NodeEditor::GetNodePosition(blockId);
    const auto nodeSize        = ax::NodeEditor::GetNodeSize(blockId);
    const auto textRectPadding = ImGui::GetStyle().ItemInnerSpacing.x;
    const auto textSize        = ImGui::CalcTextSize(tagLabel.data(), tagLabel.data() + tagLabel.size());
    const auto topLeft         = nodePosition;
    const auto topRight        = nodePosition + ImVec2{nodeSize.x, 0.f};
    const auto availableSpace  = topRight.x - topLeft.x;
    if (textSize.x + (textRectPadding * 2.f) > availableSpace) {
        return;
    }

    const auto      lineHeight                   = ImGui::GetFrameHeight();
    constexpr float maxLabelSpaceFromLeft        = 30.f;
    constexpr float labelSpaceFromLeftPercentage = 0.2f;
    const auto      spacingLeft                  = std::min(availableSpace * labelSpaceFromLeftPercentage, maxLabelSpaceFromLeft);
    const auto      topLeftOfRect                = topLeft + ImVec2{spacingLeft, -(lineHeight / 2.f)};

    const auto  schedulerColorU32 = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().flowgraphSubgraphBorder);
    const auto  textColorU32      = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().flowgraphSubgraphBorderText);
    const auto* currentWindow     = ImGui::GetCurrentWindow();
    currentWindow->DrawList->AddRectFilled(topLeftOfRect, topLeftOfRect + textSize + ImVec2{textRectPadding * 2.f, 0.f}, schedulerColorU32);
    currentWindow->DrawList->AddText(topLeftOfRect + ImVec2{textRectPadding, 0.f}, textColorU32, tagLabel.data(), tagLabel.data() + tagLabel.size());
}

void FlowgraphEditor::drawBoundingBoxExterior(const BoundingBox& canvasSpacingBoundingBox) {
    constexpr static float fadeLengthSeconds = 1.0;
    const auto             alphaPercentage   = std::clamp(this->_timeSpentHoldingPin, 0.f, fadeLengthSeconds) / fadeLengthSeconds;

    // while in canvas space, ImGui::GetMousePos() returns the canvas space value and does not need to be converted
    if (!canvasSpacingBoundingBox.contains(ImGui::GetMousePos())) {
        this->_wasHoveringBoundingBoxExteriorThisFrame = true;
        if (this->_timeSpentHoldingPin < fadeLengthSeconds) { // accelerate quickly to the background being fully faded in
            this->_timeSpentHoldingPin += ImGui::GetIO().DeltaTime * 4;
        }
        // user dragging connection outside of bounding box.
        ax::NodeEditor::Suspend();
        if (auto tooltip = IMW::ToolTip{}) {
            ImGui::TextUnformatted("Release to export this pin...");
        }
        ax::NodeEditor::Resume();
    }

    // border hover fade animation
    auto          outlineAlpha       = static_cast<std::uint8_t>(LookAndFeel::getColorAlphaU8(&Palette::flowgraphBoundingBoxExteriorSelectionOutline) * alphaPercentage);
    auto          outerAlpha         = static_cast<std::uint8_t>(LookAndFeel::getColorAlphaU8(&Palette::flowgraphBoundingBoxExteriorSelection) * alphaPercentage);
    std::uint32_t outlineOpaqueColor = LookAndFeel::getColorU32Opaque(&Palette::flowgraphBoundingBoxExteriorSelectionOutline);
    std::uint32_t outerOpaqueColor   = LookAndFeel::getColorU32Opaque(&Palette::flowgraphBoundingBoxExteriorSelection);
    float         outlineThickness   = LookAndFeel::instance().flowgraph.flowgraphBoundingBoxExteriorSelectionOutlineThickness;
    {
        auto       animPercentage = std::clamp(_timeSpentHoveringBoundingBoxExterior, 0.f, borderExteriorHoverFadeTransitionDurationSeconds) / borderExteriorHoverFadeTransitionDurationSeconds;
        const auto lerpColor      = [animPercentage](std::uint32_t coloru32, std::uint32_t targetu32) { //
            return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(coloru32), ImGui::ColorConvertU32ToFloat4(targetu32), animPercentage));
        };
        const auto castLerp = [animPercentage]<typename T>(T color, T target) { //
            return static_cast<T>(std::lerp(static_cast<float>(color), static_cast<float>(target), animPercentage));
        };

        // blend to hovered colors/alpha/outline thickness based on how long hovering exterior
        outlineOpaqueColor = lerpColor(outlineOpaqueColor, LookAndFeel::getColorU32Opaque(&Palette::flowgraphBoundingBoxExteriorSelectionOutlineHovered));
        outerOpaqueColor   = lerpColor(outerOpaqueColor, LookAndFeel::getColorU32Opaque(&Palette::flowgraphBoundingBoxExteriorSelectionHovered));
        outerAlpha         = castLerp(outerAlpha, LookAndFeel::getColorAlphaU8(&Palette::flowgraphBoundingBoxExteriorSelectionHovered));
        outlineAlpha       = castLerp(outlineAlpha, LookAndFeel::getColorAlphaU8(&Palette::flowgraphBoundingBoxExteriorSelectionOutlineHovered));
        outlineThickness   = castLerp(outlineThickness, LookAndFeel::instance().flowgraph.flowgraphBoundingBoxExteriorSelectionOutlineThicknessHovered);
    }

    const auto outlineColor = rgbToImGuiABGR(outlineOpaqueColor, outlineAlpha);
    const auto outerColor   = rgbToImGuiABGR(outerOpaqueColor, outerAlpha);

    auto* const drawlist = ImGui::GetWindowDrawList();

    // consider "visible region" to be the whole window, expanding above the opendigitizer title bar and outside margins
    const auto visibleRegionMin = ax::NodeEditor::ScreenToCanvas(ImGui::GetWindowPos());
    const auto visibleRegionMax = ax::NodeEditor::ScreenToCanvas(ImGui::GetWindowPos() + ImGui::GetWindowSize());

    // draw some rectangles to demarcate the area to drag to do port exporting
    const auto& bb = canvasSpacingBoundingBox;
    drawlist->AddRect({bb.minX, bb.minY}, {bb.maxX, bb.maxY}, outlineColor, 0, ImDrawFlags_None, outlineThickness); // outline
    drawlist->AddRectFilled(visibleRegionMin, {bb.minX, visibleRegionMax.y}, outerColor);                           // left side column
    drawlist->AddRectFilled({bb.maxX, visibleRegionMin.y}, visibleRegionMax, outerColor);                           // right side column
    drawlist->AddRectFilled({bb.minX, visibleRegionMin.y}, {bb.maxX, bb.minY}, outerColor);                         // top middle between the two columns
    drawlist->AddRectFilled({bb.minX, bb.maxY}, {bb.maxX, visibleRegionMax.y}, outerColor);                         // bottom middle between the two columns
}

FlowgraphEditor::NodeDrawResult FlowgraphEditor::drawNode( //
    UiGraphBlock&                 block,                   //
    std::span<const UiGraphPort*> inputPorts,              //
    std::span<const UiGraphPort*> outputPorts,             //
    float                         pinHorizontalPadding     //
) {
    const auto            blockId        = ax::NodeEditor::NodeId(std::addressof(block));
    const auto            isGroup        = block.isScheduler() || block.isGraph();
    const ImVec4          borderColor    = isGroup ? LookAndFeel::instance().palette().flowgraphSubgraphBorder : ax::NodeEditor::GetStyle().Colors[ax::NodeEditor::StyleColor_NodeBorder];
    const auto            borderWidthVar = IMW::NodeEditor::StyleFloatVar(ax::NodeEditor::StyleVar_NodeBorderWidth, isGroup ? 3.0f : ax::NodeEditor::GetStyle().NodeBorderWidth);
    const auto            borderColorVar = IMW::NodeEditor::StyleColor(ax::NodeEditor::StyleColor_NodeBorder, borderColor);
    IMW::NodeEditor::Node node(blockId);

    if (isGroup) {
        this->drawComputeDomainTag(block);
    }

    const auto minimumBlockSize    = LookAndFeel::instance().flowgraph.minimumBlockSize;
    const auto blockScreenPosition = ImGui::GetCursorScreenPos();
    auto       blockBottomY{blockScreenPosition.y + minimumBlockSize.y}; // we have to keep track of the Node Size ourselves

    // Draw block title
    // blockTypeName is from BLOCK_ID which respects the `alias` parameter sent when registering blocks, blockName is generated by full typename
    ImGui::TextUnformatted(block.blockTypeName.c_str());
    auto blockSize = ax::NodeEditor::GetNodeSize(blockId);

    // Draw block properties
    {
        IMW::Font font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);
        for (const auto& [propertyKey, propertyValue] : block.blockSettings) {
            if (propertyKey == "description" || propertyKey.contains("::")) {
                continue;
            }

            const auto& currentPropertyMetaInformation = block.blockSettingsMetaInformation[std::string(propertyKey)];
            if (!currentPropertyMetaInformation.isVisible) {
                continue;
            }
            std::string value = valToString(propertyValue);
            ImGui::Text("%s: %s", currentPropertyMetaInformation.description.c_str(), value.c_str());
        }

        ImGui::Spacing();

        const bool isFilter = _filterBlock == std::addressof(block);

        // Make radio-button a bit smaller since we also made the properties smaller, looks huge otherwise
        IMW::StyleVar    styleVar(ImGuiStyleVar_FramePadding, GImGui->Style.FramePadding - ImVec2{0, 3});
        IMW::ChangeStrId changeId(block.blockUniqueName.c_str());

        if (ImGui::RadioButton("Filter", isFilter)) {
            if (isFilter) {
                _filterBlock = nullptr;
            } else {
                _filterBlock = std::addressof(block);
            }
        }
    }

    blockBottomY = std::max(blockBottomY, ImGui::GetCursorPosY());

    // Register ports with node editor, actual drawing comes later
    auto registerPins = [this, &pinHorizontalPadding, &blockSize](auto& ports, auto position, auto pinType) {
        if (pinType == ax::NodeEditor::PinKind::Output) {
            position.x += blockSize.x - pinHorizontalPadding;
        }

        const float blockY  = position.y - ax::NodeEditor::GetStyle().NodePadding.y;
        const bool  isInput = pinType == ax::NodeEditor::PinKind::Input;

        for (std::size_t i = 0; i < ports.size(); ++i) {
            auto portDisplayName = exportedPortShortenedDisplayName(ports[i], _exportPortTargetBlock);
            auto info            = calculatePinDrawInfo(portDisplayName, i, ports.size(), position.x, blockY, blockSize.y, isInput);
            auto pinPos          = isInput ? ImVec2{info.topLeft.x + info.size.x, info.topLeft.y} : info.topLeft;
            addPin(ax::NodeEditor::PinId(ports[i]), pinType, pinPos, info.size);
        }
    };

    ImVec2 position = {blockScreenPosition.x - pinHorizontalPadding, blockScreenPosition.y};
    registerPins(inputPorts, position, ax::NodeEditor::PinKind::Input);
    blockBottomY = std::max(blockBottomY, ImGui::GetCursorPosY());

    registerPins(outputPorts, blockScreenPosition, ax::NodeEditor::PinKind::Output);
    blockBottomY = std::max(blockBottomY, ImGui::GetCursorPosY());

    ImGui::SetCursorScreenPos({position.x, blockBottomY});

    ImGui::Dummy(ImVec2(0.f, 0.f));
    return NodeDrawResult{position, blockBottomY};
}

void FlowgraphEditor::applyNodePosition(UiGraphBlock& block, std::optional<BoundingBox>& boundingBox, float pinHorizontalPadding) {
    auto blockId = ax::NodeEditor::NodeId(std::addressof(block));
    if (!block.view.has_value()) {
        auto blockSize = ax::NodeEditor::GetNodeSize(blockId);
        block.view     = UiGraphBlock::ViewData{
                .x      = block.storedXY.has_value() ? block.storedXY->x : boundingBox.value_or(defaultBoundingBox).minX,
                .y      = block.storedXY.has_value() ? block.storedXY->y : boundingBox.value_or(defaultBoundingBox).maxY,
                .width  = blockSize[0],
                .height = blockSize[1],
        };
        block.updatePosition = false;
        ax::NodeEditor::SetNodePosition(blockId, ImVec2(block.view->x, block.view->y));

        if (!boundingBox.has_value()) {
            boundingBox = defaultBoundingBox;
        }
        boundingBox->minX += blockSize[0] + pinHorizontalPadding;

        if (auto rootBlock = this->rootBlock()) {
            rootBlock->shouldRearrangeBlocks = true;
        }
    } else if (block.updatePosition) {
        block.view->x        = block.storedXY.has_value() ? block.storedXY->x : boundingBox.value_or(defaultBoundingBox).minX;
        block.view->y        = block.storedXY.has_value() ? block.storedXY->y : boundingBox.value_or(defaultBoundingBox).maxY;
        block.updatePosition = false;
        ax::NodeEditor::SetNodePosition(blockId, ImVec2(block.view->x, block.view->y));
    } else if (ax::NodeEditor::GetWasUserPositioned(blockId)) {
        if (!block.storedXY.has_value() || (block.storedXY.value().x != block.view->x || block.storedXY.value().y != block.view->y)) {
            block.submitUiConstraints();
        }
    }
}

void FlowgraphEditor::sendPinsConnectedGraphMessage(ax::NodeEditor::PinId inputPinId, ax::NodeEditor::PinId outputPinId) {
    // both are valid, let's accept link
    auto* inputPort  = inputPinId.AsPointer<UiGraphPort>();
    auto* outputPort = outputPinId.AsPointer<UiGraphPort>();

    if (inputPort->portDirection == outputPort->portDirection) {
        ax::NodeEditor::RejectNewItem();

    } else {
        if (ax::NodeEditor::AcceptNewItem()) {
            // AcceptNewItem() return true when user release mouse button.
            gr::Message message;
            message.cmd      = gr::message::Command::Set;
            message.endpoint = gr::scheduler::property::kEmplaceEdge;
            auto owner       = ownersForRoot();
            if (!owner) {
                return;
            }
            message.serviceName = owner->scheduler;

            message.data = gr::property_map{                                                                                  //
                {"_targetGraph", owner->graph},                                                                               //
                {std::pmr::string(gr::serialization_fields::EDGE_SOURCE_BLOCK), outputPort->ownerBlock->blockUniqueName},     //
                {std::pmr::string(gr::serialization_fields::EDGE_SOURCE_PORT), outputPort->portName},                         //
                {std::pmr::string(gr::serialization_fields::EDGE_DESTINATION_BLOCK), inputPort->ownerBlock->blockUniqueName}, //
                {std::pmr::string(gr::serialization_fields::EDGE_DESTINATION_PORT), inputPort->portName},                     //
                {std::pmr::string(gr::serialization_fields::EDGE_MIN_BUFFER_SIZE), gr::Size_t(4096)},                         //
                {std::pmr::string(gr::serialization_fields::EDGE_WEIGHT), 1},                                                 //
                {std::pmr::string(gr::serialization_fields::EDGE_NAME), "edge"}};

            _graphModel->sendMessage(std::move(message));
        }
    }
}

void FlowgraphEditor::handlePinDrag(BoundingBox boundingBox, ImVec4 linkColor) {
    // Handle creation action, returns true if editor want to create new object (node or link)
    if (auto creation = IMW::NodeEditor::Creation(linkColor, 1.0f)) {
        // allow the user to drag outside the bounds of a subgraph to export a port
        if (_editorLevel > 0) {
            this->drawBoundingBoxExterior(boundingBox);
        }
        this->_timeSpentHoldingPin += ImGui::GetIO().DeltaTime;

        ax::NodeEditor::PinId inputPinId, outputPinId;
        if (ax::NodeEditor::QueryNewLink(&outputPinId, &inputPinId)) {
            // QueryNewLink returns true if editor wants to create new link between pins.
            //
            // Link can be created only for two valid pins, it is up to you to
            // validate if connection make sense. Editor is happy to make any.
            //
            // Link always goes from input to output. User may choose to drag
            // link from output pin or input pin. This determines which pin ids
            // are valid and which are not:
            //   * input valid, output invalid - user started to drag new link from input pin
            //   * input invalid, output valid - user started to drag new link from output pin
            //   * input valid, output valid   - user dragged link over other pin, can be validated
            if (inputPinId && outputPinId) {
                this->sendPinsConnectedGraphMessage(inputPinId, outputPinId);
            }
        }

        ax::NodeEditor::PinId heldPinId;
        if (_editorLevel > 0 && ax::NodeEditor::QueryNewNode(&heldPinId)) {
            auto* port = heldPinId.AsPointer<UiGraphPort>();
            assert(port);

            if (!port->isExportedTo(_exportPortTargetBlock)) {
                _draggingPinExportRequest = ExportPortMessageData{
                    .uniqueBlockName = port->ownerBlock ? port->ownerBlock->blockUniqueName : "UNKNOWN"s,
                    .portDirection   = port->portDirection == gr::PortDirection::INPUT ? "input"s : "output"s,
                    .portName        = port->portName,
                    .exportedName    = (port->ownerBlock ? port->ownerBlock->blockName : "UNKNOWN"s) + "." + port->portName,
                    .exportFlag      = true,
                };
            }
        }
    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (_draggingPinExportRequest && !boundingBox.contains(ImGui::GetMousePos())) {
            requestExportPort(*_draggingPinExportRequest);
        }
        _draggingPinExportRequest  = {};
        this->_timeSpentHoldingPin = 0.f;
    }
}

void FlowgraphEditor::drawGraph(const ImVec2& size /*, const UiGraphBlock*& filterBlock*/) {
    const auto* rootBlock = this->rootBlock();
    if (!rootBlock) {
        return;
    }

    const auto& graphBlocks = rootBlock->childBlocks;
    const auto& graphEdges  = rootBlock->childEdges;

    makeCurrent();

    IMW::NodeEditor::Editor nodeEditor(_editorName.c_str(), size);
    const auto              padding = ax::NodeEditor::GetStyle().NodePadding;

    std::optional<BoundingBox> boundingBox;
    const auto                 addRectangleToBoundingBox = [&boundingBox](ImVec2 rectPosition, ImVec2 rectSize) {
        if (boundingBox.has_value()) {
            boundingBox->addRectangle(rectPosition, rectSize);
        } else {
            boundingBox = BoundingBox{
                                .minX = rectPosition.x,
                                .minY = rectPosition.y,
                                .maxX = rectPosition.x + rectSize.x,
                                .maxY = rectPosition.y + rectSize.y,
            };
        }
    };

    // to save result of expensive recursion
    std::vector<ax::NodeEditor::NodeId> filteredOutNodes;

    // over-reserved, but minimizes allocations
    filteredOutNodes.reserve(_filterBlock ? graphBlocks.size() : 0);

    const auto transformPorts = [](const UiGraphBlock& block, const std::vector<UiGraphPort>& ports) {
        auto sortedPorts = ports | std::views::transform([](const auto& p) { return &p; }) | std::ranges::to<std::vector>();
        if (block.isScheduler() || block.isGraph()) { // regular blocks define port order by declaration order
            std::ranges::sort(sortedPorts, {}, [](auto* p) { return std::tie(p->portType, p->portName); });
        }
        return sortedPorts;
    };

    // We need to pass all blocks in order for NodeEditor to calculate
    // the sizes. Then, we can arrange those that are newly created
    for (auto& block : graphBlocks) {
        const auto blockId     = ax::NodeEditor::NodeId(block.get());
        auto       inputPorts  = transformPorts(*block, block->inputPorts());
        auto       outputPorts = transformPorts(*block, block->outputPorts());

        const bool filteredOut = _filterBlock && !_graphModel->blockInTree(*block.get(), *_filterBlock);

        // If filteredOut, set opacity to 25% until we exit the scope
        float                        originalAlpha = std::exchange(ImGui::GetStyle().Alpha, (filteredOut ? 0.25f : ImGui::GetStyle().Alpha));
        Digitizer::utils::scope_exit restoreStyle  = [&] { ImGui::GetStyle().Alpha = originalAlpha; };

        if (filteredOut) {
            filteredOutNodes.push_back(blockId);
        }

        const auto blockPosition = this->drawNode(*block, inputPorts, outputPorts, padding.x);
        const auto blockSize     = ax::NodeEditor::GetNodeSize(blockId);

        // Update bounding box
        if (block->view.has_value()) {
            auto position  = ax::NodeEditor::GetNodePosition(blockId);
            block->view->x = position[0];
            block->view->y = position[1];
            addRectangleToBoundingBox(position, blockSize);
        }

        // The input/output pins are drawn after ending the node because otherwise
        // drawing them would increase the node size, which we need to know to correctly place the
        // output pins, and that would cause the nodes to continuously grow in width
        {
            auto leftPos = blockPosition.topLeft.x - padding.x;

            ImGui::SetCursorScreenPos(blockPosition.topLeft);
            auto drawList = ax::NodeEditor::GetNodeBackgroundDrawList(blockId);

            auto drawPorts = [&](auto& ports, auto portLeftPos, bool isInput) {
                const auto& fg        = LookAndFeel::instance().flowgraph;
                const float anchorX   = portLeftPos + padding.x;
                const float blockTopY = blockPosition.topLeft.y - ax::NodeEditor::GetStyle().NodePadding.y;

                for (std::size_t i = 0; i < ports.size(); ++i) {
                    auto portExportedDisplayName = exportedPortShortenedDisplayName(ports[i], _exportPortTargetBlock);
                    auto info                    = calculatePinDrawInfo(portExportedDisplayName, i, ports.size(), anchorX, blockTopY, blockSize.y, isInput);

                    if (!portExportedDisplayName) {
                        const bool isSupressed = ports[i]->isAutoExportSuppressed();
                        drawPin(drawList, info.topLeft, ports[i]->portDirection, info.size, ports[i]->portName, ports[i]->portType, true, isSupressed);
                        continue;
                    }

                    const auto& typeStyle = FlowgraphPage::styleForDataType(ports[i]->portType);
                    const auto  textColor = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().flowgraphBg);
                    drawList->AddRectFilled(info.topLeft, info.topLeft + info.size, typeStyle.color);
                    drawList->AddRect(info.topLeft, info.topLeft + info.size, darkenOrLighten(typeStyle.color));
                    drawList->AddText(info.font, info.fontSize, ImVec2{info.topLeft.x + fg.exportedTabPaddingH, info.topLeft.y + fg.exportedTabPaddingV}, textColor, portExportedDisplayName->c_str());
                }
            };

            drawPorts(inputPorts, leftPos, true);
            drawPorts(outputPorts, leftPos + blockSize.x, false);
        }
    }

    for (auto& block : graphBlocks) {
        this->applyNodePosition(*block, boundingBox, padding.x);
    }

    const auto linkColor = ImGui::GetStyle().Colors[ImGuiCol_Text];
    for (auto& edge : graphEdges) {
        const auto sourceBlockId      = ax::NodeEditor::NodeId(edge.edgeSourcePort->ownerBlock);
        const auto destinationBlockId = ax::NodeEditor::NodeId(edge.edgeDestinationPort->ownerBlock);
        if (!std::any_of(filteredOutNodes.begin(), filteredOutNodes.end(), [&](const ax::NodeEditor::NodeId& nodeId) { //
                return nodeId == sourceBlockId || nodeId == destinationBlockId;
            })) {
            ax::NodeEditor::Link(ax::NodeEditor::LinkId(&edge), //
                ax::NodeEditor::PinId(edge.edgeSourcePort),     //
                ax::NodeEditor::PinId(edge.edgeDestinationPort), linkColor);
        }
    }

    // fade out the bounding box effect. though handlePinDrag() may call drawBoundingBoxExterior() and stop this from happening
    this->_wasHoveringBoundingBoxExteriorThisFrame = false;

    this->handlePinDrag(
        [&boundingBox]() {
            constexpr static std::size_t marginPixels = 10;
            auto                         out          = boundingBox.value_or(defaultBoundingBox);
            out.minX -= marginPixels;
            out.minY -= marginPixels;
            out.maxX += marginPixels;
            out.maxY += marginPixels;
            return out;
        }(),
        linkColor);

    // play fade animation for hovering outside the bounding box
    if (this->_wasHoveringBoundingBoxExteriorThisFrame) {
        if (this->_timeSpentHoveringBoundingBoxExterior < borderExteriorHoverFadeTransitionDurationSeconds) {
            this->_timeSpentHoveringBoundingBoxExterior += ImGui::GetIO().DeltaTime;
        }
    } else if (this->_timeSpentHoveringBoundingBoxExterior > 0) {
        this->_timeSpentHoveringBoundingBoxExterior -= ImGui::GetIO().DeltaTime;
    }
}

void FlowgraphEditor::draw(const ImVec2& contentTopLeft, const ImVec2& contentSize, bool isCurrentEditor) {
    auto* rootBlock = this->rootBlock();
    if (!rootBlock) {
        // maybe the graph doesn't exist yet, can happen after exchanging the graph + we still have an out of date root block name from the old graph
        auto* schedulerInfo = _exportPortTargetBlock ? std::get_if<UiGraphBlock::SchedulerBlockInfo>(&_exportPortTargetBlock->blockCategoryInfo) : nullptr;
        if (schedulerInfo && schedulerInfo->childrenLoaded && !_exportPortTargetBlock->childBlocks.empty()) {
            _rootBlockUniqueName = _exportPortTargetBlock->childBlocks.front()->blockUniqueName;
            rootBlock            = _exportPortTargetBlock->childBlocks.front().get();
        } else {
            return;
        }
    }

    makeCurrent();

    IMW::PushCursorPosition origCursorPos;

    ImGui::SetCursorPos(contentTopLeft);

    if (!isCurrentEditor) {
        // If this is not the enabled (top level editor) just draw the
        // flowgraph and no UI controls
        IMW::Disabled enableOnlyCurrent(!isCurrentEditor);
        drawGraph(contentSize);
        return;
    }

    const bool      horizontalSplit   = contentSize.x > contentSize.y;
    constexpr float splitterWidth     = 6;
    constexpr float halfSplitterWidth = splitterWidth / 2.f;
    const float     ratio             = components::Splitter(contentSize, horizontalSplit, splitterWidth, 0.2f, !_editPaneContext.selectedBlock());

    const auto clicked = drawButtons(contentTopLeft, contentSize,
        {
            .openNewBlockDialog       = static_cast<bool>(openNewBlockSelectorCallback),
            .openNewSubGraphDialog    = static_cast<bool>(openNewSubGraphSelectorCallback),
            .openRemoteSignalSelector = static_cast<bool>(openAddRemoteSignalCallback),
            .rearrangeBlocks          = true,
            .closeWindow              = static_cast<bool>(closeRequestedCallback),
        },
        horizontalSplit ? (ratio) : 1.0f);

    if (clicked.rearrangeBlocks) {
        sortNodes(rootBlock, true);
    }

    if (clicked.closeWindow && closeRequestedCallback) {
        closeRequestedCallback();
        return;
    }

    if (openNewBlockSelectorCallback && clicked.openNewBlockDialog) {
        openNewBlockSelectorCallback(_graphModel);
    }

    if (openNewSubGraphSelectorCallback && clicked.openNewSubGraphDialog) {
        openNewSubGraphSelectorCallback(_graphModel);
    }

    if (openAddRemoteSignalCallback && clicked.openRemoteSignalSelector) {
        openAddRemoteSignalCallback(_graphModel);
    }

    if (exportPortRequest) {
        bool shouldShowExportPortPopup = true;
        ImGui::OpenPopup("Exported port name");
        if (ImGui::BeginPopupModal("Exported port name", &shouldShowExportPortPopup, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Enter the name the port will be visible as:");
            ImGui::InputText("##Input", &exportPortTextField);

            {
                IMW::Disabled _(exportPortTextField.empty());
                if (ImGui::Button("OK")) {
                    exportPortRequest->exportedName = exportPortTextField;
                    requestExportPort(*exportPortRequest);

                    exportPortRequest.reset();
                    exportPortTextField.clear();
                    shouldShowExportPortPopup = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                exportPortRequest.reset();
                exportPortTextField.clear();
                shouldShowExportPortPopup = false;
            }
            ImGui::EndPopup();
        }
    }

    constexpr components::YesNoPopupOptions popupOptions{
        .yesText   = "Yes, unexport and disconnect all",
        .noText    = "Cancel and keep external connections",
        .titleText = "Unexporting port will disconnect external edges. Are you sure?",
    };

    constexpr static const char* unexportPortPopupId = "##Unexport port";
    if (unexportPortRequest) {
        ImGui::OpenPopup(unexportPortPopupId);

        using namespace components;
        const auto popupResult = beginYesNoPopup(unexportPortPopupId, popupOptions, ImGuiWindowFlags_AlwaysAutoResize);
        if (isPopupConfirmed(popupResult)) {
            requestExportPort(unexportPortRequest->message);
            if (unexportPortRequest->suppressAfter) {
                suppressPortAutoExport(unexportPortRequest->message);
            }
        }
        if (isPopupOpen(popupResult)) {
            ImGui::EndPopup();
        }

        if (!ImGui::IsPopupOpen(unexportPortPopupId)) {
            // popup closed, so the user's request is either cancelled or applied
            unexportPortRequest.reset();
        }
    }

    if (rootBlock->shouldRearrangeBlocks) {
        sortNodes(rootBlock, false);
    }

    auto originalFilterBlock = _filterBlock;
    drawGraph(contentSize);

    // don't open properties pane if just clicking on the radio button
    const bool filterRadioPressed = originalFilterBlock != _filterBlock;

    const auto mouseDrag         = ImLengthSqr(ImGui::GetMouseDragDelta(ImGuiMouseButton_Right));
    const auto backgroundClicked = ax::NodeEditor::GetBackgroundClickButtonIndex();

    if (!filterRadioPressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && mouseDrag < 200 && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        auto n     = ax::NodeEditor::GetHoveredNode();
        auto block = n.AsPointer<UiGraphBlock>();

        if (!block) {
            _editPaneContext.setSelectedBlock(nullptr, nullptr);
        } else if (auto ownerNames = ownersForRoot()) {
            _editPaneContext.targetGraph = ownerNames->graph;
            _editPaneContext.setSelectedBlock(block, _graphModel);
            _editPaneContext.closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay;
        }
    }

    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        auto n     = ax::NodeEditor::GetDoubleClickedNode();
        auto block = n.AsPointer<UiGraphBlock>();
        if (block && (block->isGraph() || block->isScheduler())) {
            requestGraphEdit(block);
        }
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        auto n     = ax::NodeEditor::GetHoveredNode();
        auto block = n.AsPointer<UiGraphBlock>();
        if (block) {
            ImGui::OpenPopup("block_ctx_menu");
            _selectedBlock = block;
        }
    }

    if (backgroundClicked == ImGuiMouseButton_Right && mouseDrag < 200) {
        ImGui::OpenPopup("ctx_menu");
        _contextMenuPosition = ax::NodeEditor::ScreenToCanvas(ImGui::GetMousePos());
    }

    if (auto menu = IMW::Popup("ctx_menu", 0)) {
        if (ImGui::MenuItem("Refresh graph")) {
            _graphModel->requestFullUpdate();
            _graphModel->requestAvailableBlocksTypesUpdate();
        }
    }

    if (auto menu = IMW::Popup("block_ctx_menu", 0)) {
        if (ImGui::MenuItem("Delete this block")) {
            requestBlockDeletion(_selectedBlock->blockUniqueName);
        }

        if (_selectedBlock->blockCategory == "TransparentBlockGroup" || _selectedBlock->blockCategory == "ScheduledBlockGroup") {
            if (ImGui::MenuItem("Edit block graph...")) {
                requestGraphEdit(_selectedBlock);
            }
        }

        auto typeParams = _graphModel->availableParametrizationsFor(_selectedBlock->blockTypeName);
        if (typeParams.availableParametrizations && typeParams.availableParametrizations->size() > 1) {
            if (IMW::Menu blockTypesMenu("Change type to...", /*enabled*/ true); blockTypesMenu) {
                for (const auto& availableParametrization : *typeParams.availableParametrizations) {
                    if (availableParametrization != typeParams.parametrization && ImGui::MenuItem(availableParametrization.c_str())) {
                        if (auto owner = ownersForRoot()) {
                            gr::Message message;
                            message.cmd         = gr::message::Command::Set;
                            message.endpoint    = gr::scheduler::property::kReplaceBlock;
                            message.serviceName = owner->scheduler;
                            message.data        = gr::property_map{                                         //
                                {"uniqueName", _selectedBlock->blockUniqueName},                     //
                                {"type", std::move(typeParams.baseType) + availableParametrization}, //
                                {"_targetGraph", owner->graph}};

                            _graphModel->sendMessage(std::move(message));
                        }
                    }
                }
            }
        }

        if (_editorLevel > 0) {
            this->drawPortsMenu("Input ports", "input", _selectedBlock->_inputPorts);
            this->drawPortsMenu("Output ports", "output", _selectedBlock->_outputPorts);
        }
    }

    // Create a new ImGui window for an overlay over the NodeEditor , where we can place our buttons
    // if we don't put the buttons in this overlay, the click events will go to the editor instead of the buttons
    if (horizontalSplit) {
        ImGui::SetNextWindowPos({origCursorPos.saved.x, origCursorPos.saved.y + contentSize.y - 37.f}, ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowPos({origCursorPos.saved.x, origCursorPos.saved.y + contentSize.y * (1.f - ratio) - 39.f}, ImGuiCond_Always); // on vertical, we need some extra space for the splitter
    }

    if (horizontalSplit) {
        const float w = contentSize.x * ratio;
        requestBlockControlsPanel(_editPaneContext, {contentTopLeft.x + contentSize.x - w + halfSplitterWidth, contentTopLeft.y}, {w - halfSplitterWidth, contentSize.y}, true);
    } else {
        const float h = contentSize.y * ratio;
        requestBlockControlsPanel(_editPaneContext, {contentTopLeft.x, contentTopLeft.y + contentSize.y - h + halfSplitterWidth}, {contentSize.x, h - halfSplitterWidth}, false);
    }
}

void FlowgraphEditor::drawPortsMenu(const char* text, const char* portDirection, const auto& blockPorts) {
    if (blockPorts.empty()) {
        return;
    }

    auto portsSubMenu = IMW::Menu{text, /*enabled*/ true};
    if (!portsSubMenu) {
        return;
    }

    for (const UiGraphPort& knownPort : blockPorts) {
        auto contextMenu = IMW::Menu(knownPort.portName.c_str(), true);
        if (!contextMenu) {
            continue;
        }

        this->drawPortExportOptionsMenu(knownPort, portDirection);
    }
}

void FlowgraphEditor::sortNodes(UiGraphBlock* rootBlock, bool all) {
    auto blockLevels = topologicalSort(rootBlock->childBlocks, rootBlock->childEdges);

    constexpr float ySpacing = 32;
    constexpr float xSpacing = 200;

    // We don't want the nodes to be glued to the left edge, same for top edge and y
    static const float padding = 16.f;
    float              x       = padding;
    for (auto& level : blockLevels) {
        float y          = padding;
        float levelWidth = 0;

        for (auto& block : level.blocks) {

            const auto blockId        = ax::NodeEditor::NodeId(block);
            const bool userPositioned = ax::NodeEditor::GetWasUserPositioned(blockId) || block->storedXY.has_value();
            if (all || !userPositioned) {
                ax::NodeEditor::SetNodePosition(blockId, ImVec2(x, y));
            }
            auto blockSize = ax::NodeEditor::GetNodeSize(blockId);
            y += blockSize.y + ySpacing;
            levelWidth = std::max(levelWidth, blockSize.x);
        }

        x += levelWidth + xSpacing;
    }

    rootBlock->shouldRearrangeBlocks = false;
}

void FlowgraphEditor::requestBlockDeletion(const std::string& blockName) {
    // Send message to delete block
    if (auto owner = ownersForRoot()) {
        gr::Message message;
        message.endpoint    = gr::scheduler::property::kRemoveBlock;
        message.serviceName = owner->scheduler;
        message.data        = gr::property_map{//
            {"uniqueName", blockName},  //
            {"_targetGraph", owner->graph}};
        _graphModel->sendMessage(std::move(message));
    }
}

void FlowgraphEditor::requestExportPort(const ExportPortMessageData& request) {
    gr::Message message;

    message.cmd         = gr::message::Command::Set;
    message.endpoint    = gr::graph::property::kSubgraphExportPort;
    message.serviceName = _exportPortTargetBlock->blockUniqueName;
    message.data        = gr::property_map{                  //
        {"uniqueBlockName", request.uniqueBlockName}, //
        {"portDirection", request.portDirection},     //
        {"portName", request.portName},               //
        {"exportedName", request.exportedName},       //
        {"exportFlag", request.exportFlag}};
    graphModel()->sendMessage(std::move(message));
}

bool FlowgraphEditor::hasExternalEdgesForExportedPort(const std::string& exportedName) const {
    const auto* parentGraph = _exportPortTargetBlock ? _exportPortTargetBlock->parentBlock : nullptr;
    if (!parentGraph) {
        return false;
    }

    return std::ranges::any_of(parentGraph->childEdges, [this, &exportedName](const UiGraphEdge& edge) {
        const auto matches = [&](const UiGraphPort* port) { return port && port->ownerBlock == _exportPortTargetBlock && port->portName == exportedName; };
        return matches(edge.edgeSourcePort) || matches(edge.edgeDestinationPort);
    });
}

void FlowgraphEditor::suppressPortAutoExport(const ExportPortMessageData& request) {
    auto found = _graphModel->recursiveFindBlockByUniqueName(request.uniqueBlockName);
    if (!found) {
        return;
    }
    auto& ports  = request.portDirection == "input" ? found.block->_inputPorts : found.block->_outputPorts;
    auto  portIt = std::ranges::find_if(ports, [&request](const UiGraphPort& port) { return port.portName == request.portName; });
    if (portIt != ports.end()) {
        found.block->setPortAutoExportSuppressed(*portIt, true);
    }
}

void FlowgraphEditor::autoExportUnconnectedPorts() {
    if (_editorLevel == 0) {
        return;
    }
    auto* rootBlock = this->rootBlock();
    if (!rootBlock) {
        return;
    }

    const auto& edges = rootBlock->childEdges;

    for (const auto& block : rootBlock->childBlocks) {
        auto exportUnconnected = [&](const std::vector<UiGraphPort>& ports, const std::string& direction) {
            for (const auto& port : ports) {
                if (isPortConnected(port, edges)) {
                    continue;
                }
                if (port.isExportedTo(_exportPortTargetBlock)) {
                    continue;
                }
                if (port.isAutoExportSuppressed()) {
                    continue;
                }
                requestExportPort(ExportPortMessageData{
                    .uniqueBlockName = block->blockUniqueName,
                    .portDirection   = direction,
                    .portName        = port.portName,
                    .exportedName    = getDefaultExportedName(&port),
                    .exportFlag      = true,
                });
            }
        };

        exportUnconnected(block->_inputPorts, "input");
        exportUnconnected(block->_outputPorts, "output");
    }
}

FlowgraphPage::FlowgraphPage(std::shared_ptr<opencmw::client::RestClient> restClient) : _restClient{std::move(restClient)} {}

FlowgraphPage::~FlowgraphPage() = default;

void FlowgraphPage::reset() {
    if (_dashboard) {
        _dashboard->graphModel.rootBlock.childEdges.clear();
        _dashboard->graphModel.rootBlock.childBlocks.clear();
    }

    _editors.clear();
}

void FlowgraphPage::pushEditor(std::string name, UiGraphModel& graphModel, UiGraphBlock* rootBlock) {
    assert(rootBlock && "An editor needs to have a root block defined");
    assert(!rootBlock->blockUniqueName.empty() && !rootBlock->blockCategory.empty() && "An editor needs to have a root block defined and initialized");
    std::println("FlowgraphPage::pushEditor name {} rootBlock {} category {}", name, //
        rootBlock->blockUniqueName, rootBlock->blockCategory);

    auto& editor = _editors.emplace_back(name, graphModel, rootBlock, _editors.size());

    editor.updateStyle();
    editor.requestBlockControlsPanel = requestBlockControlsPanel;

    editor.requestGraphEdit = [&](UiGraphBlock* block) { pushEditor(block->blockUniqueName, graphModel, block); };

    // This lambda is owned by editor, so it is safe to take it by reference
    editor.openNewBlockSelectorCallback = [this, &editor](UiGraphModel* /*_graphModel*/) {
        if (auto owner = editor.ownersForRoot()) {
            _newBlockSelector.data = editor.graphModel()->knownBlockTypes;
            _newBlockSelector.open(owner->scheduler, owner->graph);
        }
    };

    // This lambda is owned by editor, so it is safe to take it by reference
    editor.openNewSubGraphSelectorCallback = [this, &editor](UiGraphModel* /*_graphModel*/) {
        if (auto owner = editor.ownersForRoot()) {
            _newBlockSelector.data = editor.graphModel()->knownSchedulerTypes;
            _newBlockSelector.open(owner->scheduler, owner->graph);
        }
    };

    // We can add remote signals only to the root graph
    if (_remoteSignalSelector && _editors.size() == 1) {
        editor.openAddRemoteSignalCallback = [&](UiGraphModel* /*_graphModel*/) { _remoteSignalSelector->open(); };
    }

    if (_editors.size() > 1) {
        editor.closeRequestedCallback = [this] {
            currentEditor().autoExportUnconnectedPorts();
            popEditor();
        };
    }
}

void FlowgraphPage::popEditor() {
    _editors.pop_back();
    if (_editors.size() > 0) {
        currentEditor().graphModel()->requestFullUpdate();
    }
}

void FlowgraphPage::updateStyle() {
    for (auto& editor : _editors) {
        editor.updateStyle();
    }
}

const FlowgraphPage::DataTypeStyle& FlowgraphPage::styleForDataType(std::string_view type) {
    using DataTypeStyleMap = std::unordered_map<std::string, DataTypeStyle, opendigitizer::TransparentStringHash, std::equal_to<>>;

    auto withDataSetColors = [](DataTypeStyleMap&& map) {
        DataTypeStyleMap result;
        while (map.begin() != map.end()) {
            auto it          = map.begin();
            auto datasetName = "gr::DataSet<"s + it->first + ">"s;

            result[datasetName]               = it->second;
            result[datasetName].datasetMarker = true;

            result.insert(map.extract(it));
        }

        return result;
    };

    static auto styleForDataTypeLight = withDataSetColors({
        {"float32"s, {0xffF57C00}}, //
        {"float64"s, {0xff00BCD4}}, //

        {"int8"s, {0xffD500F9}},                  //
        {"int16"s, {0xffFFEB3B}},                 //
        {"int32"s, {0xff009688}},                 //
        {"int64"s, {0xffCDDC39}},                 //
        {"uint8"s, {0xffD500F9, true}},           //
        {"uint16"s, {0xffFFEB3B, true}},          //
        {"uint32"s, {0xff009688, true}},          //
        {"uint64"s, {0xffCDDC39, true}},          //
                                                  //
        {"std::complex<float32>"s, {0xff2196F3}}, //
        {"std::complex<float64>"s, {0xff795548}}, //
                                                  //
        {"std::complex<int8>"s, {0xff9C27B0}},    //
        {"std::complex<int16>"s, {0xffFFC107}},   //
        {"std::complex<int32>"s, {0xff4CAF50}},   //
        {"std::complex<int64>"s, {0xff8BC34A}},   //

        {"gr::DataSet<float32>"s, {0xffF57C00, false, true}}, //
        {"gr::DataSet<float64>"s, {0xff00BCD4, false, true}}, //
                                                              //
        {"gr::Message"s, {0xffDBDBDB}},                       //

        {"Bits"s, {0xffEA80FC}},          //
        {"BusConnection"s, {0xffffffff}}, //
        {"Wildcard"s, {0xffffffff}},      //
        {"Untyped"s, {0xffffffff}},       //
    });

    static auto styleForDataTypeDark = withDataSetColors({
        {"float32"s, {0xff0a83ff}}, //
        {"float64"s, {0xffff432b}}, //

        {"int8"s, {0xff2aff06}},         //
        {"int16"s, {0xff0014c4}},        //
        {"int32"s, {0xffff6977}},        //
        {"int64"s, {0xff3223c6}},        //
        {"uint8"s, {0xff2aff06, true}},  //
        {"uint16"s, {0xff0014c4, true}}, //
        {"uint32"s, {0xffff6977, true}}, //
        {"uint64"s, {0xff3223c6, true}}, //

        {"std::complex<float32>"s, {0xffde690c}}, //
        {"std::complex<float64>"s, {0xff86aab8}}, //

        {"std::complex<int8>"s, {0xff63d84f}},  //
        {"std::complex<int16>"s, {0xff003ef8}}, //
        {"std::complex<int32>"s, {0xffb350af}}, //
        {"std::complex<int64>"s, {0xff743cb5}}, //

        {"gr::DataSet<float64>"s, {0xffff432b}}, //
        {"gr::DataSet<float32>"s, {0xff0a83ff}}, //

        {"gr::Message"s, {0xff242424}}, //

        {"Bits"s, {0xff158003}},          //
        {"BusConnection"s, {0xff000000}}, //
        {"Wildcard"s, {0xff000000}},      //
        {"Untyped"s, {0xff000000}},       //

    });

    auto& map = LookAndFeel::instance().style == LookAndFeel::Style::Light ? styleForDataTypeLight : styleForDataTypeDark;
    auto  it  = map.find(type);
    if (it == map.cend()) {
        std::print("Warning: Color not defined for {}\n", type);
        static DataTypeStyle none{0x00000000};
        return none;
    } else {
        return it->second;
    }
}

void FlowgraphPage::drawNodeEditorTab() {
    auto contentSize    = ImGui::GetContentRegionAvail();
    auto contentTopLeft = ImGui::GetCursorPos();

    std::size_t level = 0UZ;
    // for (const auto& [level, editor] : std::views::enumerate(_editors)) { TODO: Use once we bump to an EMSDK that supports it
    for (auto& editor : _editors) {
        if (level != 0) {
            static constexpr float levelPadding = 16.0f;
            contentTopLeft += ImVec2(levelPadding, levelPadding);
            contentSize -= ImVec2(2 * levelPadding, 2 * levelPadding);
        }

        if (&editor == &_editors.back()) {
            editor.draw(contentTopLeft, contentSize, &editor == &_editors.back());

            if (_remoteSignalSelector) {
                for (const auto& selectedRemoteSignal : _remoteSignalSelector->drawAndReturnSelected()) {
                    _dashboard->addRemoteSignal(selectedRemoteSignal);
                }
            }

            _newBlockSelector.draw();
        }
        level++;
    }
}

void FlowgraphPage::drawLocalYamlTab() {
    if (ImGui::Button("Reset") || _currentTabIsFlowGraph) {
        // Reload yaml whenever "Local - YAML" tab is selected
        _currentTabIsFlowGraph = false;

        if (!_editors.empty()) {
            if (auto owner = _editors.front().ownersForRoot()) {
                gr::Message message;
                message.cmd         = gr::message::Command::Get;
                message.endpoint    = gr::scheduler::property::kGraphGRC;
                message.serviceName = owner->scheduler;
                _dashboard->graphModel.sendMessage(std::move(message));
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
        if (auto owner = _editors.front().ownersForRoot()) {
            gr::Message message;
            message.cmd         = gr::message::Command::Set;
            message.endpoint    = gr::scheduler::property::kGraphGRC;
            message.data        = gr::property_map{{"value", _dashboard->graphModel.m_localFlowgraphGrc}};
            message.serviceName = owner->scheduler;
            _dashboard->graphModel.sendMessage(std::move(message));
        }
    }

    ImGui::InputTextMultiline("##grc", &_dashboard->graphModel.m_localFlowgraphGrc, ImGui::GetContentRegionAvail());
}

void FlowgraphPage::drawRemoteYamlTab(Dashboard::Service& service) {
    std::string tabTitle = "Remote YAML for " + service.name;
    if (auto item = IMW::TabItem(tabTitle.c_str(), nullptr, 0)) {
        if (ImGui::Button("Reload from service")) {
            service.reload();
        }
        ImGui::SameLine();
        if (ImGui::Button("Execute on service")) {
            service.execute();
        }

        // TODO: For demonstration purposes only, remove
        // once we have a proper server-side graph editor
        // if (::getenv("DIGITIZER_UI_SHOW_SERVER_TEST_BUTTONS")) {
        ImGui::SameLine();
        if (ImGui::Button("Create a block")) {
            service.emplaceBlock("gr::basic::DataSink", "float");
        }
        // }

        ImGui::InputTextMultiline("##grc", &service.grc, ImGui::GetContentRegionAvail());
    }
}

void FlowgraphPage::draw() noexcept {
    // TODO: tab-bar is optional and should be eventually eliminated to optimise viewing area for data
    IMW::TabBar tabBar("maintabbar", 0);

    if (auto item = IMW::TabItem("Local", nullptr, 0)) {
        if (!_editors.empty()) {
            _currentTabIsFlowGraph = true;
            drawNodeEditorTab();
        } else if (!_dashboard->graphModel.rootBlock.blockUniqueName.empty()) {
            // We don't have an editor until the root graph is loaded
            pushEditor("rootBlock node editor", _dashboard->graphModel, std::addressof(_dashboard->graphModel.rootBlock));
        }
    }

    if (auto item = IMW::TabItem("Local - YAML", nullptr, 0)) {
        drawLocalYamlTab();
    }

    for (auto& service : _dashboard->services) {
        drawRemoteYamlTab(service);
    }
}

} // namespace DigitizerUi
