#include "imguiutils.h"
#include "app.h"
#include "flowgraph.h"

#include <fmt/format.h>

#include "flowgraph/datasink.h"
#include <any>
#include <charconv>
#include <imgui_internal.h>

namespace ImGuiUtils {
class InputKeypad {
    static inline constexpr const char *keypad_name = "KeypadX";
    static inline constexpr size_t      buffer_size = 64;

    bool                                visible     = true;
    std::string                         edit_buffer;
    std::any                            prev_value;

private:
    enum class ReturnState {
        None,
        Change,
        Accept,
        Discard = -1
    };
    InputKeypad() {
        edit_buffer.reserve(buffer_size);
    };
    static auto &Get() {
        static InputKeypad instance;
        return instance;
    }

public:
    template<typename EdTy>
        requires std::integral<EdTy> || std::floating_point<EdTy> || std::same_as<std::string, EdTy>
    static bool Edit(const char *label, EdTy *value) {
        if (!label || !value) return false;
        if constexpr (std::floating_point<EdTy>)
            ImGui::DragFloat(label, static_cast<float *>(value), 0.1f);
        else if constexpr (std::integral<EdTy>)
            ImGui::DragInt(label, static_cast<int *>(value));
        else
            ImGui::InputText(label, value);

        return Get().EditImpl(label, value);
    }
    static bool Visible() noexcept { return Get().visible; }

private:
    ReturnState DrawKeypad(const char *label) noexcept {
        ReturnState r = ReturnState::None;
        // Always center this window when appearing
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(360, 400));

        if (ImGui::BeginPopupModal(keypad_name, &visible, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", label);
            r = Keypad("Keypad Input");
            ImGui::EndPopup();
        }
        return r;
    }

    template<typename EdTy>
    bool EditImpl(const char *label, EdTy *value) noexcept {
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            visible = true;
            ImGui::OpenPopup(keypad_name);
            prev_value.emplace<EdTy>(*value); // save for discard
            edit_buffer.clear();
            fmt::format_to(std::back_inserter(edit_buffer), "{}\0", *value);
        }

        ReturnState r = DrawKeypad(edit_buffer.c_str());

        if (r == ReturnState::Discard) {
            *value  = any_cast<EdTy>(prev_value);
            visible = false;
            return true;
        }

        if (r == ReturnState::Accept) {
            if constexpr (std::same_as<std::string, EdTy>) {
                *value = edit_buffer;
            } else if constexpr (std::floating_point<EdTy>) {
                EdTy converted = 0;
                *value         = strtof(edit_buffer.c_str(), nullptr);
            } else {
                EdTy             converted = 0;
                std::string_view a{ edit_buffer };
                auto [ptr, ec] = std::from_chars(a.begin(), a.end(), converted);
                if (ec != std::errc())
                    return false;
                *value = converted;
            }
            visible = false;
        }

        return r == ReturnState::Accept;
    }

    ReturnState Keypad(const char *label) noexcept {
        struct Guard {
            ~Guard() { ImGui::EndChild(); }
        };
        ImVec2      csize = ImGui::GetContentRegionAvail();
        float       n     = floorf(csize.y / 5); // height / 5 button rows

        ImGuiStyle &style = ImGui::GetStyle();

        if (ImGui::BeginChild(label, ImVec2(n * 5 + style.WindowPadding.x, n * 5), true)) {
            Guard g;
            csize = ImGui::GetContentRegionAvail();            // now inside this child
            n     = floorf(csize.y / 5 - style.ItemSpacing.y); // button size
            ImVec2 bsize(n, n);                                // buttons are square
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6);

            char k = 0;
            if (ImGui::Button("ESC", bsize) || ImGui::IsKeyPressedMap(ImGuiKey_Escape)) {
                k = 'X';
            }
            ImGui::SameLine();
            if (ImGui::Button("<-", bsize) || ImGui::IsKeyPressedMap(ImGuiKey_Backspace)) {
                k = 'B';
            }
            ImGui::SameLine();
            if (ImGui::Button("AC", bsize) || ImGui::IsKeyPressedMap(ImGuiKey_Delete)) {
                k = 'C';
            }
            ImGui::SameLine();
            if (ImGui::Button("±", bsize)) {
                k = 'S';
            }
            ImGui::SameLine();
            if (ImGui::Button("√", bsize)) {
                k = 'Q';
            }

            // Second row
            if (ImGui::Button("7", bsize)
                    || ImGui::IsKeyPressedMap(ImGuiKey_7)
                    || ImGui::IsKeyPressedMap(ImGuiKey_Keypad7)) {
                k = '7';
            }
            ImGui::SameLine();
            if (ImGui::Button("8", bsize)
                    || ImGui::IsKeyPressedMap(ImGuiKey_8)
                    || ImGui::IsKeyPressedMap(ImGuiKey_Keypad8)) {
                k = '8';
            }
            ImGui::SameLine();
            if (ImGui::Button("9", bsize)
                    || ImGui::IsKeyPressedMap(ImGuiKey_9)
                    || ImGui::IsKeyPressedMap(ImGuiKey_Keypad9)) {
                k = '9';
            }
            ImGui::SameLine();
            if (ImGui::Button("/", bsize)
                    || ImGui::IsKeyPressedMap(ImGuiKey_Slash)
                    || ImGui::IsKeyPressedMap(ImGuiKey_KeypadDivide)) {
                k = '/';
            }
            ImGui::SameLine();
            if (ImGui::Button("%", bsize)) {
                k = '%';
            }

            if (ImGui::Button("4", bsize)
                    || ImGui::IsKeyPressedMap(ImGuiKey_4)
                    || ImGui::IsKeyPressedMap(ImGuiKey_Keypad4)) {
                k = '4';
            }
            ImGui::SameLine();
            if (ImGui::Button("5", bsize)
                    || ImGui::IsKeyPressedMap(ImGuiKey_5)
                    || ImGui::IsKeyPressedMap(ImGuiKey_Keypad5)) {
                k = '5';
            }
            ImGui::SameLine();
            if (ImGui::Button("6", bsize)
                    || ImGui::IsKeyPressedMap(ImGuiKey_6)
                    || ImGui::IsKeyPressedMap(ImGuiKey_Keypad6)) {
                k = '6';
            }
            ImGui::SameLine();
            if (ImGui::Button("*", bsize) || ImGui::IsKeyPressedMap(ImGuiKey_KeypadMultiply)) {
                k = '*';
            }
            ImGui::SameLine();
            if (ImGui::Button("1/x", bsize) || ImGui::IsKeyPressedMap(ImGuiKey_KeypadMultiply)) {
                k = 'R';
            }

            if (ImGui::Button("1", bsize)
                    || ImGui::IsKeyPressedMap(ImGuiKey_1)
                    || ImGui::IsKeyPressedMap(ImGuiKey_Keypad1)) {
                k = '1';
            }
            ImGui::SameLine();
            if (ImGui::Button("2", bsize)
                    || ImGui::IsKeyPressedMap(ImGuiKey_2)
                    || ImGui::IsKeyPressedMap(ImGuiKey_Keypad2)) {
                k = '2';
            }
            ImGui::SameLine();
            if (ImGui::Button("3", bsize)
                    || ImGui::IsKeyPressedMap(ImGuiKey_3)
                    || ImGui::IsKeyPressedMap(ImGuiKey_Keypad3)) {
                k = '3';
            }
            ImGui::SameLine();
            if (ImGui::Button("-", bsize) || ImGui::IsKeyPressedMap(ImGuiKey_KeypadSubtract)) {
                k = '-';
            }
            ImGui::SameLine();
            if (ImGui::Button("=", { bsize.x, bsize.y * 2.0f + style.WindowPadding.y / 2 })
                    || ImGui::IsKeyPressedMap(ImGuiKey_Enter)
                    || ImGui::IsKeyPressedMap(ImGuiKey_KeypadEnter)) {
                k = 'E';
            }

            ImGui::SetCursorPosY(4.0f * (bsize.y + style.WindowPadding.y) - style.WindowPadding.y);
            if (ImGui::Button("0", { bsize[0] * 2.0f + style.WindowPadding.x, bsize[1] })
                    || ImGui::IsKeyPressedMap(ImGuiKey_0)
                    || ImGui::IsKeyPressedMap(ImGuiKey_Keypad0)) {
                k = '0';
            }
            ImGui::SameLine();
            if (ImGui::Button(".", bsize) || ImGui::IsKeyPressedMap(ImGuiKey_Period)) {
                k = '.';
            }
            ImGui::SameLine();
            if (ImGui::Button("+", bsize) || ImGui::IsKeyPressedMap(ImGuiKey_KeypadAdd)) {
                k = '+';
            }

            ImGui::PopStyleVar();

            // logic
            switch (k) {
            case 0: return ReturnState::None;
            case 'E': return ReturnState::Accept;
            case 'X': return ReturnState::Discard;
            case 'B':
                edit_buffer.pop_back();
                return ReturnState::Change;
            case 'C':
                edit_buffer.clear();
                return ReturnState::Change;
            default:
                edit_buffer.push_back(k); // add k to the string
                return ReturnState::Change;
            }
        }
        return ReturnState::None;
    }
};

DialogButton drawDialogButtons(bool okEnabled) {
    float y = ImGui::GetContentRegionAvail().y;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y - 20);
    ImGui::Separator();

    if (!okEnabled) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Ok") || (okEnabled && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
        ImGui::CloseCurrentPopup();
        return DialogButton::Ok;
    }
    if (!okEnabled) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
        return DialogButton::Cancel;
    }
    return DialogButton::None;
}

struct Splitter {
    enum class State {
        Hidden,
        AnimatedForward,
        AnimatedBackward,
        Shown
    } anim_state;
    float start_ratio = 0.0f;
    float ratio       = 0.0f;
    float speed       = 0.02f;

    void  move(float max, bool forward = true) noexcept {
        if (forward)
            move_forward(max);
        else
            move_backward();
    }

    void move_forward(float max) noexcept {
        if (anim_state == State::Shown)
            return;

        anim_state = State::AnimatedForward;
        if (ratio / max >= 0.7f)
            speed = 0.01f;

        ratio += speed;
        if (ratio >= max) {
            ratio      = max;
            anim_state = State::Shown;
            speed      = 0.02f;
        }
    }
    void move_backward() noexcept {
        if (anim_state == State::Hidden)
            return;

        anim_state = State::AnimatedBackward;
        ratio -= speed;
        if (ratio <= 0.0f)
            reset();
    }

    void reset() noexcept {
        anim_state  = State::Hidden;
        start_ratio = 0.0f;
        ratio       = 0.0f;
    }
    bool is_hidden() const noexcept {
        return anim_state == State::Hidden;
    }
} splitter_state;

float splitter(ImVec2 space, bool vertical, float size, float defaultRatio, bool reset) {
    float startRatio = splitter_state.start_ratio;

    splitter_state.move(defaultRatio, !reset);
    if (splitter_state.is_hidden())
        return 0.0f;

    float s = vertical ? space.x : space.y;
    auto  w = s * splitter_state.ratio;
    if (vertical) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + s - w - size / 2.f);
    } else {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + s - w - size / 2.f);
    }

    ImGui::BeginChild("##c");
    ImGui::Button("##sep", vertical ? ImVec2{ size, space.y } : ImVec2{ space.x, size });

    const auto cursor = vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS;
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(cursor);
    }

    if (ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(cursor);
        const auto delta     = ImGui::GetMouseDragDelta();
        splitter_state.ratio = startRatio - (vertical ? delta.x : delta.y) / s;
    } else {
        splitter_state.start_ratio = splitter_state.ratio;
    }
    ImGui::EndChild();
    return splitter_state.ratio;
}

void drawBlockControlsPanel(BlockControlsPanel &ctx, const ImVec2 &pos, const ImVec2 &frameSize, bool verticalLayout) {
    using namespace DigitizerUi;

    auto size = frameSize;
    if (ctx.block) {
        if (ctx.closeTime < std::chrono::system_clock::now()) {
            ctx = {};
            return;
        }

        auto &app = App::instance();
        ImGui::PushFont(app.fontIconsSolid);
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing() * 1.5f;
        ImGui::PopFont();

        auto resetTime = [&]() {
            ctx.closeTime = std::chrono::system_clock::now() + app.editPaneCloseDelay;
        };

        const auto itemSpacing    = ImGui::GetStyle().ItemSpacing;

        auto       calcButtonSize = [&](int numButtons) -> ImVec2 {
            if (verticalLayout) {
                return { (size.x - float(numButtons - 1) * itemSpacing.x) / float(numButtons), lineHeight };
            }
            return { lineHeight, (size.y - float(numButtons - 1) * itemSpacing.y) / float(numButtons) };
        };

        ImGui::SetCursorPos(pos);

        if (ImGui::BeginChildFrame(1, size, ImGuiWindowFlags_NoScrollbar)) {
            size = ImGui::GetContentRegionAvail();

            // don't close the panel while the mouse is hovering it or edits are made.
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
                    || InputKeypad::Visible()) {
                resetTime();
            }

            auto duration = float(std::chrono::duration_cast<std::chrono::milliseconds>(ctx.closeTime - std::chrono::system_clock::now()).count()) / float(std::chrono::duration_cast<std::chrono::milliseconds>(app.editPaneCloseDelay).count());
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Button]));
            ImGui::ProgressBar(1.f - duration, { size.x, 3 });
            ImGui::PopStyleColor();

            auto minpos      = ImGui::GetCursorPos();
            size             = ImGui::GetContentRegionAvail();

            int outputsCount = 0;
            {
                const char *prevString = verticalLayout ? "\uf062" : "\uf060";
                for (const auto &out : ctx.block->outputs()) {
                    outputsCount += out.connections.size();
                }
                if (outputsCount == 0) {
                    ImGuiUtils::DisabledGuard dg;
                    ImGui::PushFont(app.fontIconsSolid);
                    ImGui::Button(prevString, calcButtonSize(1));
                    ImGui::PopFont();
                } else {
                    const auto buttonSize = calcButtonSize(outputsCount);

                    ImGui::BeginGroup();
                    int id = 1;
                    for (auto &out : ctx.block->outputs()) {
                        for (const auto *conn : out.connections) {
                            ImGui::PushID(id++);

                            ImGui::PushFont(app.fontIconsSolid);
                            if (ImGui::Button(prevString, buttonSize)) {
                                ctx.block = conn->ports[1]->block;
                            }
                            ImGui::PopFont();
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", conn->ports[1]->block->name.c_str());
                            }
                            ImGui::PopID();
                            if (verticalLayout) {
                                ImGui::SameLine();
                            }
                        }
                    }
                    ImGui::EndGroup();
                }
            }

            if (!verticalLayout) {
                ImGui::SameLine();
            }

            {
                // Draw the two add block buttons
                ImGui::BeginGroup();
                const auto buttonSize = calcButtonSize(2);
                {
                    ImGuiUtils::DisabledGuard dg(ctx.mode != BlockControlsPanel::Mode::None || outputsCount == 0);
                    ImGui::PushFont(app.fontIconsSolid);
                    if (ImGui::Button("\uf055", buttonSize)) {
                        if (outputsCount > 1) {
                            ImGui::OpenPopup("insertBlockPopup");
                        } else {
                            [&]() {
                                int index = 0;
                                for (auto &out : ctx.block->outputs()) {
                                    for (auto *conn : out.connections) {
                                        ctx.insertFrom      = conn->ports[0];
                                        ctx.insertBefore    = conn->ports[1];
                                        ctx.breakConnection = conn;
                                        return;
                                    }
                                    ++index;
                                }
                            }();
                            ctx.mode = BlockControlsPanel::Mode::Insert;
                        }
                    }
                    ImGui::PopFont();
                    setItemTooltip("%s", "Insert new block before the next");

                    if (ImGui::BeginPopup("insertBlockPopup")) {
                        int index = 0;
                        for (auto &out : ctx.block->outputs()) {
                            for (auto *conn : out.connections) {
                                auto text = fmt::format("Before block '{}'", conn->ports[1]->block->name);
                                if (ImGui::Selectable(text.c_str())) {
                                    ctx.insertBefore    = conn->ports[1];
                                    ctx.mode            = BlockControlsPanel::Mode::Insert;
                                    ctx.insertFrom      = conn->ports[0];
                                    ctx.breakConnection = conn;
                                }
                            }
                            ++index;
                        }
                        ImGui::EndPopup();
                    }

                    if (verticalLayout) {
                        ImGui::SameLine();
                    }
                }

                ImGui::PushFont(app.fontIconsSolid);
                DisabledGuard dg(ctx.mode != BlockControlsPanel::Mode::None || ctx.block->outputs().empty());
                if (ImGui::Button("\uf0fe", buttonSize)) {
                    if (ctx.block->outputs().size() > 1) {
                        ImGui::OpenPopup("addBlockPopup");
                    } else {
                        ctx.mode       = BlockControlsPanel::Mode::AddAndBranch;
                        ctx.insertFrom = &ctx.block->outputs()[0];
                    }
                }
                ImGui::PopFont();
                setItemTooltip("%s", "Add new block");

                if (ImGui::BeginPopup("addBlockPopup")) {
                    int index = 0;
                    for (const auto &out : ctx.block->type->outputs) {
                        if (ImGui::Selectable(out.name.c_str())) {
                            ctx.insertFrom = &ctx.block->outputs()[index];
                            ctx.mode       = BlockControlsPanel::Mode::AddAndBranch;
                        }
                        ++index;
                    }
                }

                ImGui::EndGroup();

                if (!verticalLayout) {
                    ImGui::SameLine();
                }
            }

            if (ctx.mode != BlockControlsPanel::Mode::None) {
                ImGui::BeginGroup();

                auto listSize = verticalLayout ? ImVec2(size.x, 200) : ImVec2(200, size.y - ImGui::GetFrameHeightWithSpacing());
                auto ret      = filteredListBox(
                        "blocks", BlockType::registry().types(), [](auto &it) -> std::pair<BlockType *, std::string> {
                            if (it.second->inputs.size() != 1 || it.second->outputs.size() != 1) {
                                return {};
                            }
                            return std::pair{ it.second.get(), it.first };
                        },
                        listSize);

                {
                    DisabledGuard dg(!ret.has_value());
                    if (ImGui::Button("Ok")) {
                        BlockType  *selected = ret->first;
                        auto        name     = fmt::format("{}({})", selected->name, ctx.block->name);
                        auto        block    = selected->createBlock(name);

                        Connection *c1;
                        if (ctx.mode == BlockControlsPanel::Mode::Insert) {
                            // mode Insert means that the new block should be added in between this block and the next one.
                            // put the new block in between this block and the following one
                            c1 = app.dashboard->localFlowGraph.connect(&block->outputs()[0], ctx.insertBefore);
                            app.dashboard->localFlowGraph.connect(ctx.insertFrom, &block->inputs()[0]);
                            app.dashboard->localFlowGraph.disconnect(ctx.breakConnection);
                            ctx.breakConnection = nullptr;
                        } else {
                            // mode AddAndBranch means the new block should feed its data to a new sink to be also plotted together with the old one.
                            auto *newsink = app.dashboard->createSink();
                            c1            = app.dashboard->localFlowGraph.connect(&block->outputs()[0], &newsink->inputs()[0]);
                            app.dashboard->localFlowGraph.connect(ctx.insertFrom, &block->inputs()[0]);

                            auto source = std::find_if(app.dashboard->sources().begin(), app.dashboard->sources().end(), [&](const auto &s) {
                                return s.block == newsink;
                            });

                            app.dashboardPage.newPlot(app.dashboard.get());
                            app.dashboard->plots().back().sources.push_back(&*source);
                        }
                        ctx.block = block.get();

                        app.dashboard->localFlowGraph.addBlock(std::move(block));
                        ctx.mode = BlockControlsPanel::Mode::None;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    ctx.mode = BlockControlsPanel::Mode::None;
                }

                ImGui::EndGroup();

                if (!verticalLayout) {
                    ImGui::SameLine();
                }
            }

            ImGui::BeginChild("Settings", verticalLayout ? ImVec2(size.x, ImGui::GetContentRegionAvail().y - lineHeight - itemSpacing.y) : ImVec2(ImGui::GetContentRegionAvail().x - lineHeight - itemSpacing.x, size.y), true,
                    ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(ctx.block->name.c_str());
            ImGuiUtils::blockParametersControls(ctx.block, verticalLayout);

            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
                resetTime();
            }
            ImGui::EndChild();

            ImGui::SetCursorPos(minpos);

            // draw the button(s) that go to the previous block(s).
            const char *nextString = verticalLayout ? "\uf063" : "\uf061";
            ImGui::PushFont(app.fontIconsSolid);
            if (ctx.block->inputs().empty()) {
                auto buttonSize = calcButtonSize(1);
                if (verticalLayout) {
                    ImGui::SetCursorPosY(ImGui::GetContentRegionMax().y - buttonSize.y);
                } else {
                    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - buttonSize.x);
                }
                ImGuiUtils::DisabledGuard dg;
                ImGui::Button(nextString, buttonSize);
            } else {
                auto buttonSize = calcButtonSize(ctx.block->inputs().size());
                if (verticalLayout) {
                    ImGui::SetCursorPosY(ImGui::GetContentRegionMax().y - buttonSize.y);
                } else {
                    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - buttonSize.x);
                }

                ImGui::BeginGroup();
                int id = 1;
                for (auto &in : ctx.block->inputs()) {
                    ImGui::PushID(id++);
                    ImGuiUtils::DisabledGuard dg(in.connections.empty());

                    if (ImGui::Button(nextString, buttonSize)) {
                        ctx.block = in.connections.front()->ports[0]->block;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::PopFont();
                        ImGui::SetTooltip("%s", in.connections.front()->ports[0]->block->name.c_str());
                        ImGui::PushFont(app.fontIconsSolid);
                    }
                    ImGui::PopID();
                    if (verticalLayout) {
                        ImGui::SameLine();
                    }
                }
                ImGui::EndGroup();
            }
            ImGui::PopFont();
        }

        ImGui::EndChild();
    }
}

void blockParametersControls(DigitizerUi::Block *b, bool verticalLayout, const ImVec2 &size) {
    const auto availableSize = ImGui::GetContentRegionAvail();

    auto       storage       = ImGui::GetStateStorage();
    ImGui::PushID("block_controls");

    const auto &style     = ImGui::GetStyle();
    const auto  indent    = style.IndentSpacing;
    const auto  textColor = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_Text]);

    for (int i = 0; i < b->type->parameters.size(); ++i) {
        const auto &p  = b->type->parameters[i];

        auto        id = ImGui::GetID(p.label.c_str());
        ImGui::PushID(int(id));
        auto *enabled = storage->GetBoolRef(id, true);

        ImGui::BeginGroup();
        const auto curpos = ImGui::GetCursorPos();
        ImGui::SetCursorPosY(curpos.y + ImGui::GetFrameHeightWithSpacing());
        ImGui::BeginGroup();

        if (*enabled) {
            char label[64];
            snprintf(label, sizeof(label), "##parameter_%d", i);

            if (auto *e = std::get_if<DigitizerUi::BlockType::EnumParameter>(&p.impl)) {
                auto value = std::get<DigitizerUi::Block::EnumParameter>(b->parameters()[i]);

                for (int j = 0; j < e->options.size(); ++j) {
                    auto &opt      = e->options[j];

                    bool  selected = value.optionIndex == j;
                    if (ImGui::RadioButton(opt.c_str(), selected)) {
                        value.optionIndex = j;
                        b->setParameter(i, value);
                        b->update();
                    }
                }
            } else if (auto *ip = std::get_if<DigitizerUi::Block::NumberParameter<int>>(&b->parameters()[i])) {
                int val = ip->value;
                ImGui::SetNextItemWidth(100);
                if (InputKeypad::Edit(label, &val)) {
                    b->setParameter(i, DigitizerUi::Block::NumberParameter<int>{ val });
                    b->update();
                }
            } else if (auto *fp = std::get_if<DigitizerUi::Block::NumberParameter<float>>(&b->parameters()[i])) {
                float val = fp->value;
                ImGui::SetNextItemWidth(100);
                if (InputKeypad::Edit(label, &val)) {
                    b->setParameter(i, DigitizerUi::Block::NumberParameter<float>{ val });
                    b->update();
                }
            } else if (auto *rp = std::get_if<DigitizerUi::Block::RawParameter>(&b->parameters()[i])) {
                std::string val = rp->value;
                ImGui::SetNextItemWidth(100);

                if (InputKeypad::Edit(label, &val)) {
                    b->setParameter(i, DigitizerUi::Block::RawParameter{ std::move(val) });
                    b->update();
                }
            }
        }

        ImGui::EndGroup();
        ImGui::SameLine(0, 0);

        auto        width = verticalLayout ? availableSize.x : ImGui::GetCursorPosX() - curpos.x;
        const auto *text  = *enabled || verticalLayout ? p.label.c_str() : "";
        width             = std::max(width, indent + ImGui::CalcTextSize(text).x + style.FramePadding.x * 2);

        if (*enabled) {
            ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_TabUnfocusedActive]);
        }

        ImGui::SetCursorPos(curpos);

        float height = !verticalLayout && !*enabled ? availableSize.y : 0.f;
        if (ImGui::Button("##nothing", { width, height })) {
            *enabled = !*enabled;
        }
        ImGui::PopStyleColor();

        setItemTooltip("%s", p.label.c_str());

        ImGui::SetCursorPos(curpos + ImVec2(style.FramePadding.x, style.FramePadding.y));
        ImGui::RenderArrow(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), textColor, *enabled ? ImGuiDir_Down : ImGuiDir_Right, 1.0f);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
        if (*enabled || verticalLayout) {
            ImGui::TextUnformatted(p.label.c_str());
        }

        ImGui::EndGroup();

        if (!verticalLayout) {
            ImGui::SameLine();
        }

        ImGui::PopID();
    }
    ImGui::PopID();
}

} // namespace ImGuiUtils
