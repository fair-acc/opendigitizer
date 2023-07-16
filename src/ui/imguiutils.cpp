#include "imguiutils.h"
#include "app.h"
#include "flowgraph.h"

#include <fmt/format.h>

#include "calculator.h"
#include "flowgraph/datasink.h"
#include <any>
#include <charconv>
#include <imgui_internal.h>

namespace ImGuiUtils {
class InputKeypad {
    static inline constexpr const char *keypad_name = "KeypadX";
    static inline constexpr size_t      buffer_size = 64;

    bool                                visible     = true;
    size_t                              parenthesi  = 0;
    std::string                         edit_buffer;
    std::any                            prev_value;

    Token                               last_token;

private:
    enum class ReturnState {
        None,
        Change,
        Accept,
        Discard = -1
    };

    enum class Button {
        NoButton,
        Dot,
        Sign,
        AC,
        Backspace,
        Enter,
        Escape,

        POpen   = '(',
        PClose  = ')',

        Add     = '+',
        Sub     = '-',
        Mul     = '*',
        Div     = '/',
        Button0 = '0',
        Button1 = '1',
        Button2 = '2',
        Button3 = '3',
        Button4 = '4',
        Button5 = '5',
        Button6 = '6',
        Button7 = '7',
        Button8 = '8',
        Button9 = '9',

        Percent,
        Rcp,
        Sqrt,

        Sin,
        Cos,
        Tan,
        Sinh,
        Cosh,
        Tanh,
        Pow = '^',
    };

    template<Button btn_ty, ImGuiKey_... subs_keys>
    Button static UIButton(const char *label, ImVec2 size, Button old_value = Button::NoButton) {
        return ImGui::Button(label, size) || (ImGui::IsKeyPressedMap(subs_keys) || ...) ? btn_ty : old_value;
    }
    constexpr static Button Select(Button vold, Button vnew) {
        return vnew == Button::NoButton ? vold : vnew;
    }

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
        ImGui::SetNextWindowSize(ImVec2(360, 480));

        if (ImGui::BeginPopupModal(keypad_name, &visible, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", label);
            r = Keypad("Keypad Input");
            if (r == ReturnState::Change) {
                last_token = ::last_token(edit_buffer);
            }
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
            last_token = ::last_token(edit_buffer);
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
        float       n     = floorf(csize.y / 6); // height / 5 button rows

        ImGuiStyle &style = ImGui::GetStyle();

        if (ImGui::BeginChild(label, ImVec2(n * 5 + style.WindowPadding.x, n * 6), true)) {
            Guard g;
            csize = ImGui::GetContentRegionAvail();            // now inside this child
            n     = floorf(csize.y / 6 - style.ItemSpacing.y); // button size
            ImVec2 bsize(n, n);                                // buttons are square
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6);

            Button key = Button::NoButton;
            using enum Button;

            key = UIButton<Sin>("sin", bsize, key);
            ImGui::SameLine();
            key = UIButton<Cos>("cos", bsize, key);
            ImGui::SameLine();
            key = UIButton<Tan>("tan", bsize, key);
            ImGui::SameLine();
            key = UIButton<POpen>("(", bsize, key);
            ImGui::SameLine();
            key = UIButton<PClose>(")", bsize, key);

            key = UIButton<Escape, ImGuiKey_Escape>("ESC", bsize, key);
            ImGui::SameLine();
            key = UIButton<Backspace, ImGuiKey_Backspace>("<-", bsize, key);
            ImGui::SameLine();
            key = UIButton<AC, ImGuiKey_Delete>("AC", bsize, key);
            ImGui::SameLine();
            key = UIButton<Sign>("±", bsize, key);
            ImGui::SameLine();
            key = UIButton<Sqrt>("Sqrt", bsize, key);

            key = UIButton<Button7, ImGuiKey_7, ImGuiKey_Keypad7>("7", bsize, key);
            ImGui::SameLine();
            key = UIButton<Button8, ImGuiKey_8, ImGuiKey_Keypad8>("8", bsize, key);
            ImGui::SameLine();
            key = UIButton<Button9, ImGuiKey_9, ImGuiKey_Keypad9>("9", bsize, key);
            ImGui::SameLine();
            key = UIButton<Div, ImGuiKey_Slash, ImGuiKey_KeypadDivide>("/", bsize, key);
            ImGui::SameLine();
            key = UIButton<Pow>("^", bsize, key);

            key = UIButton<Button4, ImGuiKey_4, ImGuiKey_Keypad4>("4", bsize, key);
            ImGui::SameLine();
            key = UIButton<Button5, ImGuiKey_5, ImGuiKey_Keypad5>("5", bsize, key);
            ImGui::SameLine();
            key = UIButton<Button6, ImGuiKey_6, ImGuiKey_Keypad6>("6", bsize, key);
            ImGui::SameLine();
            key = UIButton<Mul, ImGuiKey_KeypadMultiply>("*", bsize, key);
            ImGui::SameLine();
            key = UIButton<Rcp>("1/x", bsize, key);

            key = UIButton<Button1, ImGuiKey_1, ImGuiKey_Keypad1>("1", bsize, key);
            ImGui::SameLine();
            key = UIButton<Button2, ImGuiKey_2, ImGuiKey_Keypad2>("2", bsize, key);
            ImGui::SameLine();
            key = UIButton<Button3, ImGuiKey_3, ImGuiKey_Keypad3>("3", bsize, key);
            ImGui::SameLine();
            key = UIButton<Sub, ImGuiKey_KeypadSubtract>("-", bsize, key);
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, { 11.f / 255.f, 89.f / 255.f, 191.f / 255.f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 1.0f, 1.0f, 1.0f });
            key = UIButton<Enter, ImGuiKey_Enter, ImGuiKey_KeypadEnter>("Enter", { bsize.x, bsize.y * 2.0f + style.WindowPadding.y / 2 }, key);
            ImGui::PopStyleColor(2);

            ImGui::SetCursorPosY(5.0f * (bsize.y + style.WindowPadding.y) - style.WindowPadding.y);
            key = UIButton<Button0, ImGuiKey_0, ImGuiKey_Keypad0>("0", { bsize[0] * 2.0f + style.WindowPadding.x, bsize[1] }, key);
            ImGui::SameLine();
            key = UIButton<Dot, ImGuiKey_Period>(".", bsize, key);
            ImGui::SameLine();
            key = UIButton<Add, ImGuiKey_KeypadAdd>("+", bsize, key);

            ImGui::PopStyleVar();

            // logic
            switch (key) {
            case Button::NoButton: return ReturnState::None;
            case Button::Enter: {
                if (last_token.type != TType::tt_const && last_token.type != TType::tt_pclose)
                    return ReturnState::None;
                if (only_token(edit_buffer))
                    return ReturnState::Accept;
                auto result = evaluate(edit_buffer);
                if (!result) return ReturnState::None;
                edit_buffer = fmt::format("{}", result.value());
                return ReturnState::Change;
            }
            case Button::Escape: return ReturnState::Discard;
            case Button::Backspace:
                if (last_token.type == TType::tt_const)
                    edit_buffer.pop_back();
                else
                    edit_buffer.erase(edit_buffer.end() - last_token.range.size(), edit_buffer.end());

                if (last_token.range.data() != edit_buffer.data() && edit_buffer.back() == '-')
                    edit_buffer.pop_back();
                return ReturnState::Change;
            case Button::AC:
                edit_buffer.clear();
                return ReturnState::Change;

            case Button::Sign: {
                if (last_token.type == TType::tt_const) {
                    if (last_token.range.data() != edit_buffer.data() && last_token.range.data()[-1] == '-')
                        edit_buffer.erase(edit_buffer.end() - last_token.range.size() - 1);
                    else
                        edit_buffer.insert(edit_buffer.end() - last_token.range.size(), '-');
                    return ReturnState::Change;
                }

                if (last_token.type != TType::tt_pclose)
                    return ReturnState::None;

                const char *brace = nullptr;

                auto tokens = tokenize(edit_buffer);
                for (auto token = tokens.rbegin(); token != tokens.rend(); ++token) {
					if (token->type == TType::tt_pclose)
						brace++;
                    if (token->is_popen()) {
                        if (!--brace) {
							brace = token->range.data();
							break;
						}
					}
				}

                ptrdiff_t diff = abs(brace - edit_buffer.data());
                auto      iter = edit_buffer.begin() + diff;
                if (diff != 0 && *(iter - 1) == '-')
                    edit_buffer.erase(iter - 1);
                else
                    edit_buffer.insert(iter, '-');

                return ReturnState::Change;
            }
            case Button::Sqrt:
                if (last_token.type != TType::tt_const)
                    return ReturnState::None;
                {
                    std::string a{ last_token.range };
                    float       f = stof(a);
                    if (f < 0.0f) return ReturnState::None;
                    edit_buffer.erase(edit_buffer.end() - last_token.range.size(), edit_buffer.end());
                    edit_buffer.append(fmt::format("{}", sqrtf(f)));
                }
                return ReturnState::Change;
            case Button::Rcp: // reciprocate
                if (last_token.type != TType::tt_const)
                    return ReturnState::None;
                {
                    std::string a{ last_token.range };
                    float       f = stof(a);
                    if (f == 0.0f) return ReturnState::None;
                    edit_buffer.erase(edit_buffer.end() - last_token.range.size(), edit_buffer.end());
                    edit_buffer.append(fmt::format("{}", 1.0f / f));
                }
                return ReturnState::Change;
            case Button::Percent:
                if (last_token.type != TType::tt_const)
                    return ReturnState::None;
                {
                    std::string a{ last_token.range };
                    float       f = stof(a);
                    if (f == 0.0f) return ReturnState::None;
                    edit_buffer.erase(edit_buffer.end() - last_token.range.size(), edit_buffer.end());
                    edit_buffer.append(fmt::format("{}", f / 100.0f));
                }
                return ReturnState::Change;
            case Button::Dot:
                if (last_token.type != TType::tt_const || last_token.range.find('.') != std::string_view::npos)
                    break;

                edit_buffer.push_back('.'); // add k to the string
                return ReturnState::Change;
            case Button::Add:
            case Button::Sub:
            case Button::Mul:
            case Button::Div:
            case Button::Pow:
                if (!last_token.is_valid() || last_token.is_popen()) return ReturnState::None;
                if (last_token.is_operator()) {
                    *(edit_buffer.end() - 2) = char(key);
                    return ReturnState::Change;
                }

                edit_buffer.push_back(' ');
                edit_buffer.push_back(char(key));
                edit_buffer.push_back(' ');
                return ReturnState::Change;
            case Button::POpen:
                if (last_token.is_valid() && (!last_token.is_popen() && !last_token.is_operator()))
                    return ReturnState::None;

                parenthesi++;
                edit_buffer.push_back(char(key));
                return ReturnState::Change;
            case Button::PClose:
                if (!last_token.is_valid() || last_token.is_popen() || last_token.is_operator() || !parenthesi)
                    return ReturnState::None;

                parenthesi--;
                edit_buffer.push_back(char(key));
                return ReturnState::Change;
            case Button::Sin:
                if (last_token.is_valid() && (!last_token.is_popen() && !last_token.is_operator()))
                    return ReturnState::None;
                edit_buffer.append("sin(");
                parenthesi++;
                return ReturnState::Change;
            case Button::Sinh:
                if (last_token.is_valid() && (!last_token.is_popen() && !last_token.is_operator()))
                    return ReturnState::None;
                edit_buffer.append("sinh(");
                parenthesi++;
                return ReturnState::Change;
            case Button::Cos:
                if (last_token.is_valid() && (!last_token.is_popen() && !last_token.is_operator()))
                    return ReturnState::None;
                edit_buffer.append("cos(");
                parenthesi++;
                return ReturnState::Change;
            case Button::Cosh:
                if (last_token.is_valid() && (!last_token.is_popen() && !last_token.is_operator()))
                    return ReturnState::None;
                edit_buffer.append("cosh(");
                parenthesi++;
                return ReturnState::Change;
            case Button::Tan:
                if (last_token.is_valid() && (!last_token.is_popen() && !last_token.is_operator()))
                    return ReturnState::None;
                edit_buffer.append("tan(");
                parenthesi++;
                return ReturnState::Change;
            case Button::Tanh:
                if (last_token.is_valid() && (!last_token.is_popen() && !last_token.is_operator()))
                    return ReturnState::None;
                edit_buffer.append("tanh(");
                parenthesi++;
                return ReturnState::Change;
            default:
                if (!isdigit(char(key)) || last_token.type == TType::tt_pclose) return ReturnState::None;
                edit_buffer.push_back(char(key)); // add k to the string
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
    } anim_state
            = State::Hidden;
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
    [[nodiscard]] bool is_hidden() const noexcept {
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
