#include "Block.hpp"
#include "Keypad.hpp"
#include "ListBox.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include "../common/LookAndFeel.hpp"

#include "../App.hpp"
#include "../Dashboard.hpp"
#include "../DashboardPage.hpp"

namespace DigitizerUi::components {

void setItemTooltip(const char* fmt, auto&&... args) {
    if (ImGui::IsItemHovered()) {
        if constexpr (sizeof...(args) == 0) {
            ImGui::SetTooltip(fmt);
        } else {
            ImGui::SetTooltip(fmt, std::forward<decltype(args)...>(args...));
        }
    }
}

void BlockControlsPanel(Dashboard& dashboard, DashboardPage& dashboardPage, BlockControlsPanelContext& context, const ImVec2& pos, const ImVec2& frameSize, bool verticalLayout) {
    if (!context.block) {
        return;
    }
    using namespace DigitizerUi;

    auto size = frameSize;
    if (context.closeTime < std::chrono::system_clock::now()) {
        context = {};
        return;
    }

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(frameSize);
    IMW::Window blockControlsPanel("BlockControlsPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);

    const float lineHeight = [&] {
        IMW::Font font(LookAndFeel::instance().fontIconsSolid);
        return ImGui::GetTextLineHeightWithSpacing() * 1.5f;
    }();

    auto resetTime = [&]() { context.closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay; };

    const auto itemSpacing = ImGui::GetStyle().ItemSpacing;

    auto calcButtonSize = [&](int numButtons) -> ImVec2 {
        if (verticalLayout) {
            return {(size.x - float(numButtons - 1) * itemSpacing.x) / float(numButtons), lineHeight};
        }
        return {lineHeight, (size.y - float(numButtons - 1) * itemSpacing.y) / float(numButtons)};
    };

    size = ImGui::GetContentRegionAvail();

    // don't close the panel while the mouse is hovering it or edits are made.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || InputKeypad<>::isVisible()) {
        resetTime();
    }

    auto duration = float(std::chrono::duration_cast<std::chrono::milliseconds>(context.closeTime - std::chrono::system_clock::now()).count()) / float(std::chrono::duration_cast<std::chrono::milliseconds>(LookAndFeel::instance().editPaneCloseDelay).count());
    {
        // IMW::StyleColor color(ImGuiCol_PlotHistogram, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Button]));
        IMW::StyleColor color(ImGuiCol_PlotHistogram, ImGui::GetStyle().Colors[ImGuiCol_Button]);
        ImGui::ProgressBar(1.f - duration, {size.x, 3});
    }

    auto minpos = ImGui::GetCursorPos();
    size        = ImGui::GetContentRegionAvail();

    int outputsCount = 0;
#ifdef TODO_PORT
    {
        const char* prevString = verticalLayout ? "\uf062" : "\uf060";
        for (const auto& out : context.block->outputs()) {
            outputsCount += out.portConnections.size();
        }
        if (outputsCount == 0) {
            IMW::Disabled dg(true);
            IMW::Font     font(LookAndFeel::instance().fontIconsSolid);
            ImGui::Button(prevString, calcButtonSize(1));
        } else {
            const auto buttonSize = calcButtonSize(outputsCount);

            IMW::Group group;
            int        id = 1;
            // "go up" buttons: for each output of the current block, and for each connection they have, put an arrow up button
            // that switches to the connected block
            for (auto& out : context.block->outputs()) {
                for (const auto* conn : out.portConnections) {
                    IMW::ChangeId newId(id++);

                    if (IMW::Font font(LookAndFeel::instance().fontIconsSolid); ImGui::Button(prevString, buttonSize)) {
                        context.block = conn->dst.uiBlock;
                    }

                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", conn->dst.uiBlock->name.c_str());
                    }
                    if (verticalLayout) {
                        ImGui::SameLine();
                    }
                }
            }
        }
    }

    if (!verticalLayout) {
        ImGui::SameLine();
    }
#endif

#ifdef TODO_PORT
    {
        // Draw the two add block buttons
        {
            IMW::Group group;
            const auto buttonSize = calcButtonSize(2);
            {
                IMW::Disabled dg(context.mode != BlockControlsPanelContext::Mode::None || outputsCount == 0);
                if (IMW::Font font(LookAndFeel::instance().fontIconsSolid); ImGui::Button("\uf055", buttonSize)) {
                    if (outputsCount > 1) {
                        ImGui::OpenPopup("insertBlockPopup");
                    } else {
                        [&]() {
                            int index = 0;
                            for (auto& out : context.block->outputs()) {
                                for (auto* conn : out.portConnections) {
                                    context.insertFrom      = &conn->src.uiBlock->outputs()[conn->src.index];
                                    context.insertBefore    = &conn->dst.uiBlock->inputs()[conn->dst.index];
                                    context.breakConnection = conn;
                                    return;
                                }
                                ++index;
                            }
                        }();
                        context.mode = BlockControlsPanelContext::Mode::Insert;
                    }
                }
                setItemTooltip("%s", "Insert new block before the next");

                if (auto popup = IMW::Popup("insertBlockPopup", 0)) {
                    int index = 0;
                    for (auto& out : context.block->outputs()) {
                        for (auto* conn : out.portConnections) {
                            auto text = fmt::format("Before block '{}'", conn->dst.uiBlock->name);
                            if (ImGui::Selectable(text.c_str())) {
                                context.insertBefore    = &conn->dst.uiBlock->inputs()[conn->dst.index];
                                context.mode            = BlockControlsPanelContext::Mode::Insert;
                                context.insertFrom      = &conn->src.uiBlock->outputs()[conn->src.index];
                                context.breakConnection = conn;
                            }
                        }
                        ++index;
                    }
                }

                if (verticalLayout) {
                    ImGui::SameLine();
                }
            }

            {
                IMW::Font     font(LookAndFeel::instance().fontIconsSolid);
                IMW::Disabled disabled(context.mode != BlockControlsPanelContext::Mode::None || context.block->outputs().empty());
                if (ImGui::Button("\uf0fe", buttonSize)) {
                    if (context.block->outputs().size() > 1) {
                        ImGui::OpenPopup("addBlockPopup");
                    } else {
                        context.mode       = BlockControlsPanelContext::Mode::AddAndBranch;
                        context.insertFrom = &context.block->outputs()[0];
                    }
                }
            }
            setItemTooltip("%s", "Add new block");

            if (auto popup = IMW::Popup("addBlockPopup", 0)) {
                int index = 0;
                for (const auto& out : context.block->currentInstantiation().outputs) {
                    if (ImGui::Selectable(out.name.c_str())) {
                        context.insertFrom = &context.block->outputs()[index];
                        context.mode       = BlockControlsPanelContext::Mode::AddAndBranch;
                    }
                    ++index;
                }
            }
        }

        if (!verticalLayout) {
            ImGui::SameLine();
        }
    }
#endif

#ifdef TODO_PORT
    if (context.mode != BlockControlsPanelContext::Mode::None) {
        {
            IMW::Group group;

            auto listSize = verticalLayout ? ImVec2(size.x, 200) : ImVec2(200, size.y - ImGui::GetFrameHeightWithSpacing());
            auto ret      = FilteredListBox(
                "blocks", BlockRegistry::instance().types(),
                [](auto& it) -> std::pair<BlockDefinition*, std::string> {
                    auto anyInstantiation = it.second->defaultInstantiation();
                    if (anyInstantiation.inputs.size() != 1 || anyInstantiation.outputs.size() != 1) {
                        return {};
                    }
                    return std::pair{it.second.get(), it.first};
                },
                listSize);

            {
                IMW::Disabled dg(!ret.has_value());
                if (ImGui::Button("Ok")) {
                    BlockDefinition* selected = ret->first;
                    auto             name     = fmt::format("{}({})", selected->name, context.block->blockName);

                    auto block = selected->createBlock(name);

                    Connection* c1;
                    if (context.mode == BlockControlsPanelContext::Mode::Insert) {
                        // mode Insert means that the new block should be added in between this block and the next one.
                        // put the new block in between this block and the following one
                        c1 = dashboard.localFlowGraph.connect(&block->outputs()[0], context.insertBefore);
                        dashboard.localFlowGraph.connect(context.insertFrom, &block->inputs()[0]);
                        dashboard.localFlowGraph.disconnect(context.breakConnection);
                        context.breakConnection = nullptr;
                    } else {
                        // mode AddAndBranch means the new block should feed its data to a new sink to be also plotted together with the old one.
                        auto* newsink = dashboard.createSink();
                        c1            = dashboard.localFlowGraph.connect(&block->outputs()[0], &newsink->inputs()[0]);
                        dashboard.localFlowGraph.connect(context.insertFrom, &block->inputs()[0]);

                        auto source = std::ranges::find_if(dashboard.sources(), [newsink](const auto& s) { return s.blockName == newsink->name; });

                        dashboardPage.newPlot(dashboard);
                        dashboard.plots().back().sourceNames.push_back(source->name);
                    }
                    context.block = block.get();

                    dashboard.localFlowGraph.addBlock(std::move(block));
                    context.mode = BlockControlsPanelContext::Mode::None;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                context.mode = BlockControlsPanelContext::Mode::None;
            }
        }

        if (!verticalLayout) {
            ImGui::SameLine();
        }
    }
#endif

    {
        IMW::Child settings("Settings", verticalLayout ? ImVec2(size.x, ImGui::GetContentRegionAvail().y - lineHeight - itemSpacing.y) : ImVec2(ImGui::GetContentRegionAvail().x - lineHeight - itemSpacing.x, size.y), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(context.block->blockName.c_str());
        std::string_view typeName = context.block->blockTypeName;

        ImGui::TextUnformatted("<");
        ImGui::SameLine();

        // auto                     types = context.block->type().availableBaseTypes;
        // std::vector<std::string> typeNames{types.size()};
        // std::ranges::transform(types, typeNames.begin(), [](auto t) { return DataType::name(t); });
#ifdef TODO_PORT
        if (ImGui::BeginCombo("##baseTypeCombo", context.block->currentInstantiationName().c_str())) {
            for (const auto& [instantiationName, _] : context.block->type().instantiations) {
                if (ImGui::Selectable(instantiationName.c_str(), typeName == instantiationName)) {
                    context.block->flowGraph()->changeBlockDefinition(context.block, instantiationName);
                }
                if (typeName == instantiationName) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
#endif

        ImGui::SameLine();
        ImGui::TextUnformatted(">");

        BlockSettingsControls(context.block, verticalLayout);

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
            resetTime();
        }
    }

    ImGui::SetCursorPos(minpos);

#ifdef TODO_PORT
    // draw the button(s) that go to the previous block(s).
    const char* nextString = verticalLayout ? "\uf063" : "\uf061";
    IMW::Font   font(LookAndFeel::instance().fontIconsSolid);
    if (context.block->inputPorts.empty()) {
        auto buttonSize = calcButtonSize(1);
        if (verticalLayout) {
            ImGui::SetCursorPosY(ImGui::GetContentRegionMax().y - buttonSize.y);
        } else {
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - buttonSize.x);
        }
        IMW::Disabled dg(true);
        ImGui::Button(nextString, buttonSize);
    } else {
        auto buttonSize = calcButtonSize(context.block->inputs().size());
        if (verticalLayout) {
            ImGui::SetCursorPosY(ImGui::GetContentRegionMax().y - buttonSize.y);
        } else {
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - buttonSize.x);
        }

        IMW::Group group;
        int        id = 1;
        for (auto& in : context.block->inputs()) {
            IMW::ChangeId newId(id++);
            IMW::Disabled dg(in.portConnections.empty());

            if (ImGui::Button(nextString, buttonSize)) {
                context.block = in.portConnections.front()->src.uiBlock;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::PopFont();
                ImGui::SetTooltip("%s", in.portConnections.front()->src.uiBlock->name.c_str());
                ImGui::PushFont(LookAndFeel::instance().fontIconsSolid);
            }
            if (verticalLayout) {
                ImGui::SameLine();
            }
        }
    }
#endif
}

namespace {
template<class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template<typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;
} // namespace

void BlockSettingsControls(UiGraphBlock* block, bool verticalLayout, const ImVec2& size) {
    const auto availableSize = ImGui::GetContentRegionAvail();

    auto             storage = ImGui::GetStateStorage();
    IMW::ChangeStrId mainId("block_controls");

    const auto& style     = ImGui::GetStyle();
    const auto  indent    = style.IndentSpacing;
    const auto  textColor = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_Text]);

    int i = 0;
    for (const auto& p : block->blockSettings) {
        auto id = ImGui::GetID(p.first.c_str());
        ImGui::PushID(int(id));
        auto* enabled = storage->GetBoolRef(id, true);

        {
            IMW::Group topGroup;
            const auto curpos = ImGui::GetCursorPos();

            {
                IMW::Group controlGroup;

                if (*enabled) {
                    char label[64];
                    snprintf(label, sizeof(label), "##parameter_%d", i);

                    auto sendSetSettingMessage = [](auto blockUniqueName, auto key, auto value) {
                        gr::Message message;
                        message.serviceName = blockUniqueName;
                        message.endpoint    = gr::block::property::kSetting;
                        message.cmd         = gr::message::Command::Set;
                        message.data        = gr::property_map{{key, value}};
                        App::instance().sendMessage(std::move(message));
                    };

                    const bool controlDrawn = std::visit( //
                        overloaded{                       //
                            [&](float val) {
                                ImGui::SetCursorPosY(curpos.y + ImGui::GetFrameHeightWithSpacing());
                                ImGui::SetNextItemWidth(100);
                                if (InputKeypad<>::edit(label, &val)) {
                                    sendSetSettingMessage(block->blockUniqueName, p.first, val);
                                }
                                return true;
                            },
                            [&](auto&& val) {
                                using T = std::decay_t<decltype(val)>;
                                if constexpr (std::integral<T>) {
                                    auto v = int(val);
                                    ImGui::SetCursorPosY(curpos.y + ImGui::GetFrameHeightWithSpacing());
                                    ImGui::SetNextItemWidth(100);
                                    if (InputKeypad<>::edit(label, &v)) {
                                        sendSetSettingMessage(block->blockUniqueName, p.first, v);
                                    }
                                    return true;
                                } else if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view>) {
                                    ImGui::SetCursorPosY(curpos.y + ImGui::GetFrameHeightWithSpacing());
                                    std::string str(val);
                                    if (ImGui::InputText("##in", &str)) {
                                        sendSetSettingMessage(block->blockUniqueName, p.first, std::move(str));
                                    }
                                    return true;
                                }
                                return false;
                            }},
                        p.second);

                    if (!controlDrawn) {
                        ImGui::PopID();
                        continue;
                    }
                }
            }
            ImGui::SameLine(0, 0);

            auto        width = verticalLayout ? availableSize.x : ImGui::GetCursorPosX() - curpos.x;
            const auto* text  = *enabled || verticalLayout ? p.first.c_str() : "";
            width             = std::max(width, indent + ImGui::CalcTextSize(text).x + style.FramePadding.x * 2);

            {
                IMW::StyleColor(ImGuiCol_Button, style.Colors[*enabled ? ImGuiCol_ButtonActive : ImGuiCol_TabUnfocusedActive]);
                ImGui::SetCursorPos(curpos);

                float height = !verticalLayout && !*enabled ? availableSize.y : 0.f;
                if (ImGui::Button("##nothing", {width, height})) {
                    *enabled = !*enabled;
                }
            }

            setItemTooltip("%s", p.first.c_str());

            ImGui::SetCursorPos(curpos + ImVec2(style.FramePadding.x, style.FramePadding.y));
            ImGui::RenderArrow(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), textColor, *enabled ? ImGuiDir_Down : ImGuiDir_Right, 1.0f);

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
            if (*enabled || verticalLayout) {
                ImGui::TextUnformatted(p.first.c_str());
            }
        }

        if (!verticalLayout) {
            ImGui::SameLine();
        }

        ImGui::PopID();
        ++i;
    }
}

} // namespace DigitizerUi::components
