#ifndef OPENDIGITIZER_UI_COMPONENTS_HEYPAD_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_HEYPAD_HPP_

#include "../common/LookAndFeel.hpp"

#include <charconv>
#include <string>
#include <string_view>
#include <vector>

namespace DigitizerUi::components {

// from calculator.hpp
enum class TType {
    tt_none,

    tt_plus,
    tt_minus,
    tt_mul,
    tt_div,
    tt_power,

    tt_uminus,
    tt_sin,
    tt_cos,
    tt_tan,
    tt_sinh,
    tt_cosh,
    tt_tanh,

    tt_expr,
    tt_popen,
    tt_pclose,
    tt_const,
    tt_end
};

constexpr auto operator+(TType ty) {
    return static_cast<std::size_t>(ty);
}

struct ASTNode {
    TType type  = TType::tt_end;
    float value = 0.0f;
};

struct Token {
    TType                        type = TType::tt_none;
    std::string_view             range;

    [[nodiscard]] constexpr bool is_operator() const noexcept {
        return +type >= +TType::tt_plus && +type < +TType::tt_expr;
    }
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return type != TType::tt_none && type != TType::tt_end;
    }
    [[nodiscard]] constexpr bool is_popen() const noexcept {
        return type == TType::tt_popen || +type >= +TType::tt_sin && +type <= +TType::tt_tanh;
    }
};

constexpr inline std::string_view parse_float(std::string_view stream) {
    auto begin = stream.begin();
    while (begin != stream.end()) {
        if (!isdigit(*begin) && *begin != '.' && *begin != '-' && *begin != '+' && *begin != 'e')
            break;
        begin++;
    }
    return { stream.begin(), begin };
}

constexpr inline Token get_token(std::string_view stream) {
    auto begin = stream.begin();
    while (begin != stream.end()) {
        switch (*begin) {
        case '+': return {
            TType::tt_plus, { begin - 1, begin + 2 }
        };
        case '-':
            if (auto it = begin + 1; *it == ' ')
                return {
                    TType::tt_minus, { begin - 1, begin + 2 }
                };
            return {
                TType::tt_uminus, { begin, begin + 1 }
            };
        case '*': return {
            TType::tt_mul, { begin - 1, begin + 2 }
        };
        case '/': return {
            TType::tt_div, { begin - 1, begin + 2 }
        };
        case '^': return {
            TType::tt_power, { begin - 1, begin + 2 }
        };
        case '(': return {
            TType::tt_popen, { begin, begin + 1 }
        };
        case ')': return {
            TType::tt_pclose, { begin, begin + 1 }
        };
        case 's':
            if (stream.starts_with("sinh("))
                return {
                    TType::tt_sinh, { begin, begin + 5 }
                };
            return {
                TType::tt_sin, { begin, begin + 4 }
            };
        case 'c':
            if (stream.starts_with("cosh("))
                return {
                    TType::tt_cosh, { begin, begin + 5 }
                };
            return {
                TType::tt_cos, { begin, begin + 4 }
            };
        case 't':
            if (stream.starts_with("tanh("))
                return {
                    TType::tt_tanh, { begin, begin + 5 }
                };
            return {
                TType::tt_tan, { begin, begin + 4 }
            };
        }

        if (isdigit(*begin) || *begin == '.') {
            return { TType::tt_const, parse_float({ begin, stream.end() }) };
        }
        begin++;
    }
    return { TType::tt_end, "" };
}

inline Token last_token(std::string_view stream) {
    Token t;
    while (true) {
        auto t1 = get_token(stream);
        if (t1.type == TType::tt_end)
            return t;
        t = t1;
        stream.remove_prefix(t.range.size());
    }
}

inline bool only_token(std::string_view stream) {
    Token t = get_token(stream);
    stream.remove_prefix(t.range.size());
    if (t.type == TType::tt_uminus) {
        get_token(stream);
        stream.remove_prefix(t.range.size());
    }

    auto t1 = get_token(stream);
    return t1.type == TType::tt_end;
}

inline std::vector<Token> tokenize(std::string_view stream) {
    std::vector<Token> tokens;
    while (true) {
        auto t = get_token(stream);
        tokens.push_back(t);
        if (t.type == TType::tt_end)
            return tokens;
        stream.remove_prefix(t.range.size());
    }
}

struct PTable {
    enum Action : uint8_t {
        S, // shift           (<)
        R, // reduce          (>)
        E, // equal           (=)
        X, // nothing - error ( )
        A, // accept          (A)
    };

    // columns(incoming) {^}{-a}{*/}{+-}{f/(}{)}{id}{$}
    constexpr static Action precedence_table[][8] = {
        /* {^}    */ { S, S, R, R, S, R, S, R },
        /* {-a}   */ { R, X, R, R, S, R, S, R },
        /* {* }   */ { S, S, R, R, S, R, S, R },
        /* {+-}   */ { S, S, S, R, S, R, S, R },
        /* {f/(}  */ { S, S, S, S, S, E, S, X },
        /* {)}    */ { R, X, R, R, X, R, X, R },
        /* {$}    */ { S, S, S, S, S, X, S, A },
    };

public:
    constexpr static Action GetAction(TType stack, TType incoming) {
        auto table_nav = [](TType entry) {switch (entry) {
            default:
                return entry == TType::tt_popen || +entry >= +TType::tt_sin && +entry <= +TType::tt_tanh?4:-1;
        case TType::tt_power: return 0;
        case TType::tt_uminus: return 1;

        case TType::tt_mul:
        case TType::tt_div: return 2;

        case TType::tt_plus:
        case TType::tt_minus: return 3;

        case TType::tt_pclose: return 5;

        case TType::tt_end: return 6;
        } };

        int  row       = table_nav(stack);
        if (row == -1) return X;

        int col = -1;
        switch (incoming) {
        case TType::tt_const:
            col = 6;
            break;
        case TType::tt_end:
            col = 7;
            break;
        default:
            col = table_nav(incoming);
            break;
        }

        if (col == -1) return X;
        return precedence_table[row][col];
    }
};

inline std::optional<float> evaluate(std::string_view stream) {
    std::vector<ASTNode> context;
    context.reserve(64);
    context.emplace_back(); // Add end

    auto last_term = [&]() {
        auto it = context.rbegin();
        while (it->type == TType::tt_expr)
            it++;
        return it;
    };

    auto reduce = [&](auto l_term) {
        using enum TType;
        if (l_term->type == TType::tt_uminus) {
            float a = -context.back().value;
            context.pop_back();
            context.back() = { TType::tt_expr, a };
            return;
        }
        if (l_term->type == tt_pclose) {
            // remove braces
            context.pop_back();
            auto prev = last_term();
            if (prev->type == tt_popen) {
                context.erase(context.end() - 2);
                prev = last_term();
                if (prev->type != tt_uminus)
                    return;

                context.back().value = -context.back().value;
                context.erase(context.end() - 2);
                return;
            }

            auto &last_value = context.back().value;

            switch (prev->type) {
            case TType::tt_sin:
                last_value = sinf(last_value);
                break;
            case TType::tt_cos:
                last_value = cosf(last_value);
                break;
            case TType::tt_tan:
                last_value = tanf(last_value);
                break;
            case TType::tt_sinh:
                last_value = sinhf(last_value);
                break;
            case TType::tt_cosh:
                last_value = coshf(last_value);
                break;
            case TType::tt_tanh:
                last_value = tanhf(last_value);
                break;
            default:
                return;
            }
            context.erase(context.end() - 2);
            return;
        }

        float a = context.rbegin()->value;
        float b = (context.rbegin() + 2)->value;
        context.pop_back();
        context.pop_back();

        switch (l_term->type) {
        case TType::tt_plus:
            context.back().value = a + b;
            break;
        case TType::tt_minus:
            context.back().value = b - a;
            break;
        case TType::tt_mul:
            context.back().value = b * a;
            break;
        case TType::tt_div:
            context.back().value = b / a;
            break;
        case TType::tt_power:
            context.back().value = powf(b, a);
            break;
        default:
            assert(false);
        }
    };

    auto in_tk = get_token(stream);
    stream.remove_prefix(in_tk.range.size());

    while (true) {
        auto l_term = last_term();
        auto action = PTable::GetAction(l_term->type, in_tk.type);

        switch (action) {
        case PTable::E:
        case PTable::S: // Shift
            if (in_tk.type == TType::tt_const)
                context.emplace_back(ASTNode{ TType::tt_expr, stof(std::string{ in_tk.range }) });
            else
                context.emplace_back(ASTNode{ in_tk.type });

            in_tk = get_token(stream);
            stream.remove_prefix(in_tk.range.size());
            continue;
        case PTable::R: reduce(l_term); break;
        case PTable::X: return std::nullopt;
        case PTable::A: return context.back().value;
        }
    }
}

template<std::size_t BufferSize = 256>
class InputKeypad {
    static inline constexpr const char *keypad_name = "KeypadX";

    //
    bool        _visible     = true;
    bool        _altMode     = false;
    bool        _invMode     = false;
    bool        _firstUpdate = true;
    size_t      _parentheses = 0;
    std::string _editBuffer;
    std::any    _prevValue;
    Token       _lastToken;

    enum class ReturnState {
        None,
        Change,
        Accept,
        Discard
    };

    enum class Button {
        NoButton,
        Period,
        E_scientific,
        Sign,
        AC,
        Backspace,
        Enter,
        Escape,
        Alt_2nd,
        Alt_Inv,

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
        Sqr,
        Sqrt,
        Cube,
        CubeRoot,

        Sin,
        Cos,
        Tan,
        ASin,
        ACos,
        ATan,
        Sinh,
        Cosh,
        Tanh,
        ASinh,
        ACosh,
        ATanh,
        Pow = '^',
        Log,
        Ln,
        Pow10,
        PowE
        // , NewEnumValue // for testing the static_assert in the toString member
    };

    template<typename...>
    constexpr static bool always_true = true;

    template<typename T = void>
    [[nodiscard]] consteval static const char *toString(Button button) noexcept {
        using enum InputKeypad<>::Button;
        switch (button) {
        case NoButton: return " ";
        case Period: return ".";
        case E_scientific: return "EE";
        case Sign: return "±";
        case AC: return "AC";
        case Backspace: return "<-";
        case Enter: return "Enter";
        case Escape: return "Esc";
        case Alt_2nd: return "2nd";
        case Alt_Inv: return "Inv";
        case POpen: return "(";
        case PClose: return ")";
        case Add: return "+";
        case Sub: return "-";
        case Mul: return "*";
        case Div: return "/";
        case Button0: return "0";
        case Button1: return "1";
        case Button2: return "2";
        case Button3: return "3";
        case Button4: return "4";
        case Button5: return "5";
        case Button6: return "6";
        case Button7: return "7";
        case Button8: return "8";
        case Button9: return "9";
        case Percent: return "%";
        case Rcp: return "1/x";
        case Sqr: return "x²";
        case Sqrt: return "²√";
        case Cube: return "x³";
        case CubeRoot: return "³√";
        case Sin: return "sin";
        case Cos: return "cos";
        case Tan: return "tan";
        case ASin: return "asin";
        case ACos: return "acos";
        case ATan: return "atan";
        case Sinh: return "sinh";
        case Cosh: return "cosh";
        case Tanh: return "tanh";
        case ASinh: return "asinh";
        case ACosh: return "acosh";
        case ATanh: return "atanh";
        case Pow: return "^";
        case Log: return "Log";
        case Ln: return "Ln";
        case Pow10: return "10^";
        case PowE: return "e^";
        default:
            static_assert(always_true<T>, "not all possible Button enum values are mapped");
        }
    }

    enum class LinePreference {
        SameLine,
        None
    };

    template<LinePreference SameLine, Button primaryButton, ImGuiKey... keyBinding>
    [[nodiscard]] Button static keypadButton(ImVec2 size, Button oldValue) {
        if constexpr (SameLine == LinePreference::SameLine) {
            ImGui::SameLine();
        }
        return ImGui::Button(toString(primaryButton), size) || (ImGui::IsKeyPressed(keyBinding) || ...) ? primaryButton : oldValue;
    }
    template<LinePreference SameLine, Button primaryButton, Button secondaryButton, ImGuiKey... keyBinding>
    [[nodiscard]] static Button keypadButton(ImVec2 size, Button oldValue) noexcept {
        static_assert(sizeof...(keyBinding) > 1, "needs to be called with at least two keys provided - second is double-click actions");
        if constexpr (SameLine == LinePreference::SameLine) {
            ImGui::SameLine();
        }
        const bool buttonActivated = ImGui::Button(toString(primaryButton), size);

        if (constexpr auto keyList = std::array{ keyBinding... }; ImGui::IsKeyPressed(keyList[0])) {
            return primaryButton;
        } else if ((ImGui::IsKeyPressed(keyBinding) || ...)) {
            return secondaryButton;
        }

        if (buttonActivated) {
            static double lastClick = -1.0f;
            static ImVec2 lastClickPos{ -1, -1 };
            const double  time                    = ImGui::GetTime();
            const ImVec2  clickPos                = ImGui::GetMousePos();
            const bool    isWithinDoubleClickTime = lastClick >= 0.0f && time - lastClick <= ImGui::GetIO().MouseDoubleClickTime;
            const auto    isDoubleClick           = [&]() {
                return (lastClickPos.x != -1 && std::hypot(clickPos.x - lastClickPos.x, clickPos.y - lastClickPos.y) <= ImGui::GetIO().MouseDoubleClickMaxDist);
            };
            bool doubleClicked = false;
            if (isWithinDoubleClickTime && isDoubleClick()) {
                doubleClicked = true;
            }
            lastClick    = time;
            lastClickPos = clickPos;
            return doubleClicked ? secondaryButton : primaryButton;
        }

        return oldValue;
    }

    InputKeypad() {
        _editBuffer.reserve(BufferSize);
    };

    static auto &getInstance() {
        static InputKeypad instance;
        return instance;
    }

public:
    template<typename EdTy>
        requires std::integral<EdTy> || std::floating_point<EdTy> || std::same_as<std::string, EdTy>
    [[nodiscard]] static bool edit(const char *label, EdTy *value) {
        if (!label || !value) {
            return false;
        }

        if constexpr (std::floating_point<EdTy>) {
            ImGui::DragFloat(label, static_cast<float *>(value), 0.1f);
        } else if constexpr (std::integral<EdTy>) {
            ImGui::DragInt(label, static_cast<int *>(value));
        } else {
            ImGui::InputText(label, value);
        }

        return getInstance().editImpl(label, value);
    }
    static bool isVisible() noexcept { return getInstance()._visible; }

private:
    [[nodiscard]] ReturnState drawKeypadPopup(std::string &valueLabel) noexcept {
        const auto      &mainViewPort     = *ImGui::GetMainViewport();
        const ImVec2     mainViewPortSize = mainViewPort.WorkSize;
        const bool       portraitMode     = mainViewPortSize.x < mainViewPortSize.y;
        constexpr ImVec2 defaultPortraitSize{ 400.f, 600.f };
        constexpr ImVec2 defaultLandscapeSize{ 485.f, 400.f };

        if (mainViewPortSize.x > defaultPortraitSize.x && mainViewPortSize.y > defaultPortraitSize.y) { // fits on screen
            ImGui::SetNextWindowSize(defaultPortraitSize);
        } else {
            // main viewport is smaller than the default size
            if (portraitMode) { // portrait mode
                ImGui::SetNextWindowSize(ImVec2(std::clamp(defaultPortraitSize.x, .5f * defaultPortraitSize.x, mainViewPortSize.x), std::clamp(defaultPortraitSize.y, .5f * defaultPortraitSize.y, mainViewPortSize.y)));
            } else { // landscape mode
                ImGui::SetNextWindowSize(ImVec2(std::clamp(defaultLandscapeSize.x, .5f * defaultLandscapeSize.x, mainViewPortSize.x), std::clamp(defaultLandscapeSize.y, .5f * defaultLandscapeSize.y, mainViewPortSize.y)));
            }
        }
        ImGui::SetNextWindowPos(mainViewPort.GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        bool visible = _visible; // copy because BeginPopupModal set this to false when successful but needs to remain true until input is acknowledged
        if (auto popup = IMW::ModalPopup(keypad_name, &visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration)) {
            ReturnState returnState = ReturnState::None;
            if (auto keypadInput = IMW::Child("drawKeypad Input", ImVec2{}, true, 0)) {
                const ImVec2 windowSize = ImGui::GetContentRegionAvail(); // now inside this child;

                // setup number and function keypad -- global style settings
                IMW::StyleFloatVar style(ImGuiStyleVar_FrameRounding, 6.0f);
                IMW::Font          font(LookAndFeel::instance().fontBigger[LookAndFeel::instance().prototypeMode]);
                const Button       activatedKey = windowSize.x < windowSize.y ? drawPortraitKeypad(valueLabel, windowSize) : drawLandscapeKeypad(valueLabel, windowSize);
                returnState                     = processKeypadLogic(activatedKey);
            }

            switch (returnState) {
            case ReturnState::Change:
                _firstUpdate = false;
                _lastToken   = last_token(_editBuffer);
                return ReturnState::Change;
            case ReturnState::Accept:
                _firstUpdate = true;
                return ReturnState::Accept;
            case ReturnState::Discard:
                _firstUpdate = true;
                return ReturnState::Discard;
            default:
                return ReturnState::None;
            }
        }
        return ReturnState::None;
    }

    template<typename EdTy>
    [[nodiscard]] bool editImpl(const char *label, EdTy *value) noexcept {
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            _visible = true;
            ImGui::OpenPopup(keypad_name);
            _prevValue.emplace<EdTy>(*value); // save for discard
            _editBuffer.clear();
            fmt::format_to(std::back_inserter(_editBuffer), "{}\0", *value);
            _lastToken = last_token(_editBuffer);
        }

        switch (drawKeypadPopup(_editBuffer)) {
        case ReturnState::Accept:
            if constexpr (std::same_as<std::string, EdTy>) {
                *value = _editBuffer;
            } else if constexpr (std::floating_point<EdTy>) {
                EdTy converted = 0;
                *value         = strtof(_editBuffer.c_str(), nullptr);
            } else {
                EdTy             converted = 0;
                std::string_view a{ _editBuffer };
                auto [ptr, ec] = std::from_chars(a.begin(), a.end(), converted);
                if (ec != std::errc()) {
                    return false;
                }
                *value = converted;
            }
            _visible     = false;
            _firstUpdate = true;
            return true;
        case ReturnState::Discard:
            *value       = any_cast<EdTy>(_prevValue);
            _visible     = false;
            _firstUpdate = true;
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] Button drawPortraitKeypad(std::string &valueLabel, const ImVec2 &windowSize) const {
        /**
         * ┌───────────────────────┬─────┐
         * │   NumberInputField    │ ESC │
         * ├─────┬─────┬─────┬─────┼─────┤
         * │ 2nd │ sin │ cos │ tan │ <-  │
         * ├─────┼─────┼─────┼─────┼─────┤
         * │ Inv │ 1/x │ x²  │ ²√  │  ^  │
         * ├─────┼─────┼─────┼─────┼─────┤
         * │ Log │10^x │  /  │  *  │  -  │
         * ├─────┼─────┼─────┼─────┼─────┤
         * │  (  │  7  │  8  │  9  │     │
         * ├─────┼─────┼─────┼─────┤  +  │
         * │  )  │  4  │  5  │  6  │     │
         * ├─────┼─────┼─────┼─────┼─────┤
         * │ EE  │  1  │  2  │  3  │     │
         * ├─────┼─────┴─────┼─────┤  ⏎  │
         * │  ±  │     0     │  .  │     │
         * └─────┴───────────┴─────┴─────┘
         */
        constexpr float nRows = 8; // see above layout
        constexpr float nCols = 5; // see above layout
        using enum InputKeypad<>::Button;
        using enum InputKeypad<>::LinePreference;
        const ImGuiStyle &style = ImGui::GetStyle();
        const float       nx    = floorf(windowSize.x / nCols) - .5f * nCols / (nCols - 1) * style.WindowPadding.x;
        const float       ny    = floorf(windowSize.y / nRows) - .5f * nRows / (nRows - 1) * style.WindowPadding.y;
        ImVec2            buttonSize(std::min(nx, ny), std::min(nx, ny)); // buttons are square
        Button            key = Button::NoButton;                         // return value

        // setup common number editing field
        std::vector<char> buffer(valueLabel.begin(), valueLabel.end());
        buffer.resize(BufferSize);

        {
            IMW::Font font(LookAndFeel::instance().fontLarge[LookAndFeel::instance().prototypeMode]);
            // the '-1' is to accommodate the 'Esc' button
            IMW::ItemWidth itemWidth(buttonSize.x * (nCols - 1.f) + (nCols - 2.f) * style.WindowPadding.x);
            if (ImGui::InputText("##hidden", buffer.data(), buffer.size(), ImGuiInputTextFlags_CharsScientific)) {
                valueLabel = std::string(buffer.data());
                return NoButton;
            }
        }

        // the 'ESC' clause button
        {
            IMW::StyleColor button(ImGuiCol_Button, ImVec4{ 11.f / 255.f, 89.f / 255.f, 191.f / 255.f, 1.0f });
            IMW::StyleColor text(ImGuiCol_Text, ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f });
            key = keypadButton<SameLine, Escape, ImGuiKey_Escape>(buttonSize, key);
        }

        // start row 2: 2nd,sin[h],cos[h],tan[h],<-
        if (_altMode) {
            ImVec4 button_color = style.Colors[ImGuiCol_Button];
            button_color.x *= 0.6f; // R
            button_color.y *= 0.6f; // G
            button_color.z *= 0.8f; // B

            IMW::StyleColor button(ImGuiCol_Button, button_color);
            IMW::StyleColor text(ImGuiCol_Text, ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f });
            key = keypadButton<None, Alt_2nd, ImGuiKey_NumLock>(buttonSize, key);

        } else {
            key = keypadButton<None, Alt_2nd, ImGuiKey_NumLock>(buttonSize, key);
        }
        if (_altMode) {
            if (_invMode) {
                key = keypadButton<SameLine, ASinh>(buttonSize, key);
                key = keypadButton<SameLine, ACosh>(buttonSize, key);
                key = keypadButton<SameLine, ATanh>(buttonSize, key);
            } else {
                key = keypadButton<SameLine, Sinh>(buttonSize, key);
                key = keypadButton<SameLine, Cosh>(buttonSize, key);
                key = keypadButton<SameLine, Tanh>(buttonSize, key);
            }
        } else {
            if (_invMode) {
                key = keypadButton<SameLine, ASin>(buttonSize, key);
                key = keypadButton<SameLine, ACos>(buttonSize, key);
                key = keypadButton<SameLine, ATan>(buttonSize, key);
            } else {
                key = keypadButton<SameLine, Sin>(buttonSize, key);
                key = keypadButton<SameLine, Cos>(buttonSize, key);
                key = keypadButton<SameLine, Tan>(buttonSize, key);
            }
        }
        key = keypadButton<SameLine, Backspace, AC /* double-click action  */, ImGuiKey_Backspace, ImGuiKey_Delete>(buttonSize, key);

        // start row 3: Inv,1/x,x²,²√,^
        if (_invMode) {
            ImVec4 button_color = style.Colors[ImGuiCol_Button];
            button_color.x *= 0.6f; // R
            button_color.y *= 0.6f; // G
            button_color.z *= 0.8f; // B
            IMW::StyleColor button(ImGuiCol_Button, button_color);
            IMW::StyleColor text(ImGuiCol_Text, ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f });
            key = keypadButton<None, Alt_Inv, ImGuiKey_CapsLock>(buttonSize, key);
        } else {
            key = keypadButton<None, Alt_Inv, ImGuiKey_CapsLock>(buttonSize, key);
        }
        key = keypadButton<SameLine, Rcp>(buttonSize, key);
        if (_altMode) {
            key = keypadButton<SameLine, Cube>(buttonSize, key);
            key = keypadButton<SameLine, CubeRoot>(buttonSize, key);
        } else {
            key = keypadButton<SameLine, Sqr>(buttonSize, key);
            key = keypadButton<SameLine, Sqrt>(buttonSize, key);
        }
        key = keypadButton<SameLine, Pow>(buttonSize, key);

        // start row 4: Log,10^x,/,*,-
        if (_altMode) {
            key = keypadButton<None, Ln>(buttonSize, key);
            key = keypadButton<SameLine, PowE>(buttonSize, key);
        } else {
            key = keypadButton<None, Log>(buttonSize, key);
            key = keypadButton<SameLine, Pow10>(buttonSize, key);
        }
        key = keypadButton<SameLine, Div, ImGuiKey_Slash, ImGuiKey_KeypadDivide>(buttonSize, key);
        key = keypadButton<SameLine, Mul, ImGuiKey_KeypadMultiply>(buttonSize, key);
        key = keypadButton<SameLine, Sub, ImGuiKey_KeypadSubtract>(buttonSize, key);

        // start row 5: (,7,8,9,+(1/2, two rows)
        key                          = keypadButton<None, POpen>(buttonSize, key);
        key                          = keypadButton<SameLine, Button7, ImGuiKey_7, ImGuiKey_Keypad7>(buttonSize, key);
        key                          = keypadButton<SameLine, Button8, ImGuiKey_8, ImGuiKey_Keypad8>(buttonSize, key);
        key                          = keypadButton<SameLine, Button9, ImGuiKey_9, ImGuiKey_Keypad9>(buttonSize, key);
        const float vpos_before_plus = ImGui::GetCursorPosY();
        key                          = keypadButton<SameLine, Add, ImGuiKey_KeypadAdd>({ buttonSize.x, buttonSize.y * 2.0f + 0.5f * style.WindowPadding.y }, key);
        ImGui::SetCursorPosY(vpos_before_plus);

        // start row 6: ),4,5,6,+(2/2, two rows)
        key = keypadButton<None, PClose>(buttonSize, key);
        key = keypadButton<SameLine, Button4, ImGuiKey_4, ImGuiKey_Keypad4>(buttonSize, key);
        key = keypadButton<SameLine, Button5, ImGuiKey_5, ImGuiKey_Keypad5>(buttonSize, key);
        key = keypadButton<SameLine, Button6, ImGuiKey_6, ImGuiKey_Keypad6>(buttonSize, key);

        // start row 7: E,1/,4,5,6,enter(1/2, two rows)
        key = keypadButton<None, E_scientific, ImGuiKey_E>(buttonSize, key);
        key = keypadButton<SameLine, Button1, ImGuiKey_1, ImGuiKey_Keypad1>(buttonSize, key);
        key = keypadButton<SameLine, Button2, ImGuiKey_2, ImGuiKey_Keypad2>(buttonSize, key);
        key = keypadButton<SameLine, Button3, ImGuiKey_3, ImGuiKey_Keypad3>(buttonSize, key);

        {
            IMW::StyleColor button(ImGuiCol_Button, ImVec4{ 11.f / 255.f, 89.f / 255.f, 191.f / 255.f, 1.0f });
            IMW::StyleColor text(ImGuiCol_Text, ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f });
            const float     vpos_before_enter = ImGui::GetCursorPosY();
            key                               = keypadButton<SameLine, Enter, ImGuiKey_Enter, ImGuiKey_KeypadEnter>({ buttonSize.x, buttonSize.y * 2.f + 0.5f * style.WindowPadding.y }, key);
            ImGui::SetCursorPosY(vpos_before_enter);
        }

        // start row 8: ±,0 (two columns),.,enter(2/2, two rows)
        key = keypadButton<None, Sign>(buttonSize, key);
        key = keypadButton<SameLine, Button0, ImGuiKey_0, ImGuiKey_Keypad0>({ buttonSize[0] * 2.f + style.WindowPadding.x, buttonSize.y }, key);
        key = keypadButton<SameLine, Period, ImGuiKey_Period, ImGuiKey_KeypadDecimal>(buttonSize, key);
        return key;
    }

    [[nodiscard]] Button drawLandscapeKeypad(std::string &valueLabel, const ImVec2 &windowSize) const {
        /**
         * ┌─────────────────────────────┬─────┬─────┐
         * │       NumberInputField      │ <-  │ ESC │
         * ├─────┬─────┬─────┬─────┬─────┼─────┼─────┤
         * │ 2nd │ Inv │ Log │10^x │  /  │  *  │  -  │
         * ├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
         * │ sin │ 1/x │  (  │  7  │  8  │  9  │     │
         * ├─────┼─────┼─────┼─────┼─────┼─────┤  +  │
         * │ cos │ x²  │  )  │  4  │  5  │  6  │     │
         * ├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
         * │ tan │ ²√  │ EE  │  1  │  2  │  3  │     │
         * ├─────┼─────┼─────┼─────┴─────┼─────┤  ⏎  │
         * │  ?  │  ^  │  ±  │     0     │  .  │     │
         * └─────┴─────┴─────┴───────────┴─────┴─────┘
         */
        constexpr float nRows = 6; // see above layout
        constexpr float nCols = 7; // see above layout
        using enum InputKeypad<>::Button;
        using enum InputKeypad<>::LinePreference;
        const ImGuiStyle &style = ImGui::GetStyle();
        const float       nx    = floorf(windowSize.x / nCols) - .5f * nCols / (nCols - 1) * style.WindowPadding.x;
        const float       ny    = floorf(windowSize.y / nRows) - .5f * nRows / (nRows - 1) * style.WindowPadding.y;
        ImVec2            buttonSize(std::min(nx, ny), std::min(nx, ny)); // buttons are square
        Button            key = Button::NoButton;                         // return value

        // setup common number editing field
        std::vector<char> buffer(valueLabel.begin(), valueLabel.end());
        buffer.resize(BufferSize);

        {
            IMW::Font font(LookAndFeel::instance().fontLarge[LookAndFeel::instance().prototypeMode]);
            // the '-2' is to accommodate the '<-' and 'Esc' button
            IMW::ItemWidth itemWidth(buttonSize.x * (nCols - 2.f) + (nCols - 3.f) * style.WindowPadding.x);
            if (ImGui::InputText("##hidden", buffer.data(), buffer.size(), ImGuiInputTextFlags_CharsScientific)) {
                valueLabel = std::string(buffer.data());
                return NoButton;
            }
        }

        // the '<-' button is exceptionally placed next to the 'ESC' button
        key = keypadButton<SameLine, Backspace, AC /* double-click action  */, ImGuiKey_Backspace, ImGuiKey_Delete>(buttonSize, key);

        // the 'ESC' clause button
        {
            IMW::StyleColor button(ImGuiCol_Button, ImVec4{ 11.f / 255.f, 89.f / 255.f, 191.f / 255.f, 1.0f });
            IMW::StyleColor text(ImGuiCol_Text, ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f });
            key = keypadButton<SameLine, Escape, ImGuiKey_Escape>(buttonSize, key);
        }

        // Row 2: 2nd,Inv,Log,10^x,/,*,-
        if (_altMode) {
            ImVec4 button_color = style.Colors[ImGuiCol_Button];
            button_color.x *= 0.6f; // R
            button_color.y *= 0.6f; // G
            button_color.z *= 0.8f; // B
            IMW::StyleColor button(ImGuiCol_Button, button_color);
            IMW::StyleColor text(ImGuiCol_Text, ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f });
            key = keypadButton<None, Alt_2nd, ImGuiKey_NumLock>(buttonSize, key);
        } else {
            key = keypadButton<None, Alt_2nd, ImGuiKey_NumLock>(buttonSize, key);
        }
        if (_invMode) {
            ImVec4 button_color = style.Colors[ImGuiCol_Button];
            button_color.x *= 0.6f; // R
            button_color.y *= 0.6f; // G
            button_color.z *= 0.8f; // B
            IMW::StyleColor button(ImGuiCol_Button, button_color);
            IMW::StyleColor text(ImGuiCol_Text, ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f });
            key = keypadButton<SameLine, Alt_Inv, ImGuiKey_CapsLock>(buttonSize, key);
        } else {
            key = keypadButton<SameLine, Alt_Inv, ImGuiKey_CapsLock>(buttonSize, key);
        }
        if (_altMode) {
            key = keypadButton<SameLine, Ln>(buttonSize, key);
            key = keypadButton<SameLine, PowE>(buttonSize, key);
        } else {
            key = keypadButton<SameLine, Log>(buttonSize, key);
            key = keypadButton<SameLine, Pow10>(buttonSize, key);
        }
        key = keypadButton<SameLine, Div, ImGuiKey_Slash, ImGuiKey_KeypadDivide>(buttonSize, key);
        key = keypadButton<SameLine, Mul, ImGuiKey_KeypadMultiply>(buttonSize, key);
        key = keypadButton<SameLine, Sub, ImGuiKey_KeypadSubtract>(buttonSize, key);

        // Row 3: sin[h],1/x,(,7,8,9,+(1/2, two rows)
        if (_altMode) {
            if (_invMode) {
                key = keypadButton<None, ASinh>(buttonSize, key);
            } else {
                key = keypadButton<None, Sinh>(buttonSize, key);
            }
        } else {
            if (_invMode) {
                key = keypadButton<None, ASin>(buttonSize, key);
            } else {
                key = keypadButton<None, Sin>(buttonSize, key);
            }
        }
        key                          = keypadButton<SameLine, Rcp>(buttonSize, key);
        key                          = keypadButton<SameLine, POpen>(buttonSize, key);
        key                          = keypadButton<SameLine, Button7, ImGuiKey_7, ImGuiKey_Keypad7>(buttonSize, key);
        key                          = keypadButton<SameLine, Button8, ImGuiKey_8, ImGuiKey_Keypad8>(buttonSize, key);
        key                          = keypadButton<SameLine, Button9, ImGuiKey_9, ImGuiKey_Keypad9>(buttonSize, key);
        const float vpos_before_plus = ImGui::GetCursorPosY();
        key                          = keypadButton<SameLine, Add, ImGuiKey_KeypadAdd>({ buttonSize.x, buttonSize.y * 2.0f + 0.5f * style.WindowPadding.y }, key);
        ImGui::SetCursorPosY(vpos_before_plus);

        // Row 4: cos[h],x²,),5,4,6,+(2/2, two rows)
        if (_altMode) {
            key = _invMode ? keypadButton<None, ACosh>(buttonSize, key) : keypadButton<None, Cosh>(buttonSize, key);
        } else {
            key = _invMode ? keypadButton<None, ACos>(buttonSize, key) : keypadButton<None, Cos>(buttonSize, key);
        }
        if (_altMode) {
            key = keypadButton<SameLine, Cube>(buttonSize, key);
        } else {
            key = keypadButton<SameLine, Sqr>(buttonSize, key);
        }
        key = keypadButton<SameLine, PClose>(buttonSize, key);
        key = keypadButton<SameLine, Button4, ImGuiKey_4, ImGuiKey_Keypad4>(buttonSize, key);
        key = keypadButton<SameLine, Button5, ImGuiKey_5, ImGuiKey_Keypad5>(buttonSize, key);
        key = keypadButton<SameLine, Button6, ImGuiKey_6, ImGuiKey_Keypad6>(buttonSize, key);

        // Row 5: tan[h],²√,),1,2,3,enter(1/2, two rows)
        if (_altMode) {
            key = _invMode ? keypadButton<None, ATanh>(buttonSize, key) : keypadButton<None, Tanh>(buttonSize, key);
        } else {
            key = _invMode ? keypadButton<None, ATan>(buttonSize, key) : keypadButton<None, Tan>(buttonSize, key);
        }
        if (_altMode) {
            key = keypadButton<SameLine, CubeRoot>(buttonSize, key);
        } else {
            key = keypadButton<SameLine, Sqrt>(buttonSize, key);
        }
        key = keypadButton<SameLine, E_scientific, ImGuiKey_E>(buttonSize, key);
        key = keypadButton<SameLine, Button1, ImGuiKey_1, ImGuiKey_Keypad1>(buttonSize, key);
        key = keypadButton<SameLine, Button2, ImGuiKey_2, ImGuiKey_Keypad2>(buttonSize, key);
        key = keypadButton<SameLine, Button3, ImGuiKey_3, ImGuiKey_Keypad3>(buttonSize, key);
        {
            IMW::StyleColor button(ImGuiCol_Button, ImVec4{ 11.f / 255.f, 89.f / 255.f, 191.f / 255.f, 1.0f });
            IMW::StyleColor text(ImGuiCol_Text, ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f });
            const float     vpos_before_enter = ImGui::GetCursorPosY();
            key                               = keypadButton<SameLine, Enter, ImGuiKey_Enter, ImGuiKey_KeypadEnter>({ buttonSize.x, buttonSize.y * 2.f + 0.5f * style.WindowPadding.y }, key);
            ImGui::SetCursorPosY(vpos_before_enter);
        }

        // Row 7: ?,^,±,0,0,.,enter(2/2, two rows)
        key = keypadButton<None, NoButton>(buttonSize, key);
        key = keypadButton<SameLine, Pow>(buttonSize, key);
        key = keypadButton<SameLine, Sign>(buttonSize, key);
        key = keypadButton<SameLine, Button0, ImGuiKey_0, ImGuiKey_Keypad0>({ buttonSize[0] * 2.f + style.WindowPadding.x, buttonSize.y }, key);
        key = keypadButton<SameLine, Period, ImGuiKey_Period, ImGuiKey_KeypadDecimal>(buttonSize, key);

        return key;
    }

    [[nodiscard]] ReturnState processKeypadLogic(const Button &key) {
        using enum InputKeypad<>::Button;

        switch (key) {
        case NoButton: return ReturnState::None;
        case Escape: return ReturnState::Discard;
        case Enter: {
            if (_lastToken.type != TType::tt_const && _lastToken.type != TType::tt_pclose)
                return ReturnState::None;
            if (only_token(_editBuffer))
                return ReturnState::Accept;
            auto result = evaluate(_editBuffer);
            if (!result) return ReturnState::None;
            _editBuffer = fmt::format("{}", result.value());
            return ReturnState::Change;
        }

        case Backspace:
            if (_lastToken.type == TType::tt_const) {
                _editBuffer.pop_back();
            } else {
                _editBuffer.erase(_editBuffer.end() - _lastToken.range.size(), _editBuffer.end());
            }

            if (_lastToken.range.data() != _editBuffer.data() && _editBuffer.back() == '-') {
                _editBuffer.pop_back();
            }
            return ReturnState::Change;
        case AC:
            _editBuffer.clear();
            return ReturnState::Change;
        case Alt_2nd:
            _altMode = !_altMode;
            return ReturnState::Change;
        case Alt_Inv:
            _invMode = !_invMode;
            return ReturnState::Change;

        case Sign: {
            if (_lastToken.type == TType::tt_const) {
                if (_lastToken.range.data() != _editBuffer.data() && _lastToken.range.data()[-1] == '-') {
                    _editBuffer.erase(_editBuffer.end() - _lastToken.range.size() - 1);
                } else {
                    _editBuffer.insert(_editBuffer.end() - _lastToken.range.size(), '-');
                }
                return ReturnState::Change;
            }

            if (_lastToken.type != TType::tt_pclose)
                return ReturnState::None;

            const char *brace = nullptr;

            for (const auto &token : tokenize(_editBuffer)) {
                if (token.type == TType::tt_pclose) {
                    brace++;
                }
                if (token.is_popen() && (!--brace)) {
                    brace = token.range.data();
                    break;
                }
            }

            ptrdiff_t diff = abs(brace - _editBuffer.data());
            auto      iter = _editBuffer.begin() + diff;
            if (diff != 0 && *(iter - 1) == '-') {
                _editBuffer.erase(iter - 1);
            } else {
                _editBuffer.insert(iter, '-');
            }

            return ReturnState::Change;
        }
        case Sqrt:
            if (_lastToken.type != TType::tt_const)
                return ReturnState::None;
            {
                std::string a{ _lastToken.range };
                float       f = stof(a);
                if (f < 0.0f) {
                    return ReturnState::None;
                }
                _editBuffer.erase(_editBuffer.end() - _lastToken.range.size(), _editBuffer.end());
                _editBuffer.append(fmt::format("{}", sqrtf(f)));
            }
            return ReturnState::Change;
        case Rcp: // reciprocate
            if (_lastToken.type != TType::tt_const)
                return ReturnState::None;
            {
                std::string a{ _lastToken.range };
                float       f = stof(a);
                if (f == 0.0f) return ReturnState::None;
                _editBuffer.erase(_editBuffer.end() - _lastToken.range.size(), _editBuffer.end());
                _editBuffer.append(fmt::format("{}", 1.0f / f));
            }
            return ReturnState::Change;
        case Percent:
            if (_lastToken.type != TType::tt_const) {
                return ReturnState::None;
            }
            {
                std::string a{ _lastToken.range };
                float       f = stof(a);
                if (f == 0.0f) return ReturnState::None;
                _editBuffer.erase(_editBuffer.end() - _lastToken.range.size(), _editBuffer.end());
                _editBuffer.append(fmt::format("{}", f / 100.0f));
            }
            return ReturnState::Change;
        case Period:
            if (_lastToken.type != TType::tt_const || _lastToken.range.find('.') != std::string_view::npos) {
                break;
            }
            _editBuffer.push_back('.'); // add period to the string
            return ReturnState::Change;
        case E_scientific:
            if (_lastToken.type != TType::tt_const || _lastToken.range.find('.') != std::string_view::npos) {
                break;
            }
            _editBuffer.push_back('e'); // add exponential 'e' to the string
            return ReturnState::Change;
        case Add:
        case Sub:
        case Mul:
        case Div:
        case Pow:
            if (!_lastToken.is_valid() || _lastToken.is_popen()) {
                return ReturnState::None;
            }
            if (_lastToken.is_operator()) {
                *(_editBuffer.end() - 2) = char(key);
                return ReturnState::Change;
            }

            _editBuffer.push_back(' ');
            _editBuffer.push_back(char(key));
            _editBuffer.push_back(' ');
            return ReturnState::Change;
        case POpen:
            if (_lastToken.is_valid() && (!_lastToken.is_popen() && !_lastToken.is_operator())) {
                return ReturnState::None;
            }

            _parentheses++;
            _editBuffer.push_back(char(key));
            return ReturnState::Change;
        case PClose:
            if (!_lastToken.is_valid() || _lastToken.is_popen() || _lastToken.is_operator() || !_parentheses) {
                return ReturnState::None;
            }

            _parentheses--;
            _editBuffer.push_back(char(key));
            return ReturnState::Change;
        case Sin:
            if (_lastToken.is_valid() && (!_lastToken.is_popen() && !_lastToken.is_operator())) {
                return ReturnState::None;
            }
            _editBuffer.append("sin(");
            _parentheses++;
            return ReturnState::Change;
        case Sinh:
            if (_lastToken.is_valid() && (!_lastToken.is_popen() && !_lastToken.is_operator())) {
                return ReturnState::None;
            }
            _editBuffer.append("asinh(");
            _parentheses++;
            return ReturnState::Change;
        case ASin:
            if (_lastToken.is_valid() && (!_lastToken.is_popen() && !_lastToken.is_operator())) {
                return ReturnState::None;
            }
            _editBuffer.append("asin(");
            _parentheses++;
            return ReturnState::Change;
        case ASinh:
            if (_lastToken.is_valid() && (!_lastToken.is_popen() && !_lastToken.is_operator())) {
                return ReturnState::None;
            }
            _editBuffer.append("sinh(");
            _parentheses++;
            return ReturnState::Change;
        case Cos:
            if (_lastToken.is_valid() && (!_lastToken.is_popen() && !_lastToken.is_operator())) {
                return ReturnState::None;
            }
            _editBuffer.append("cos(");
            _parentheses++;
            return ReturnState::Change;
        case Cosh:
            if (_lastToken.is_valid() && (!_lastToken.is_popen() && !_lastToken.is_operator())) {
                return ReturnState::None;
            }
            _editBuffer.append("cosh(");
            _parentheses++;
            return ReturnState::Change;
        case ACos:
            if (_lastToken.is_valid() && (!_lastToken.is_popen() && !_lastToken.is_operator())) {
                return ReturnState::None;
            }
            _editBuffer.append("acos(");
            _parentheses++;
            return ReturnState::Change;
        case ACosh:
            if (_lastToken.is_valid() && (!_lastToken.is_popen() && !_lastToken.is_operator())) {
                return ReturnState::None;
            }
            _editBuffer.append("acosh(");
            _parentheses++;
            return ReturnState::Change;
        case Tan:
            if (_lastToken.is_valid() && (!_lastToken.is_popen() && !_lastToken.is_operator())) {
                return ReturnState::None;
            }
            _editBuffer.append("tan(");
            _parentheses++;
            return ReturnState::Change;
        case Tanh:
            if (_lastToken.is_valid() && (!_lastToken.is_popen() && !_lastToken.is_operator())) {
                return ReturnState::None;
            }
            _editBuffer.append("tanh(");
            _parentheses++;
            return ReturnState::Change;
        case ATan:
            if (_lastToken.is_valid() && (!_lastToken.is_popen() && !_lastToken.is_operator())) {
                return ReturnState::None;
            }
            _editBuffer.append("atan(");
            _parentheses++;
            return ReturnState::Change;
        case ATanh:
            if (_lastToken.is_valid() && (!_lastToken.is_popen() && !_lastToken.is_operator())) {
                return ReturnState::None;
            }
            _editBuffer.append("atanh(");
            _parentheses++;
            return ReturnState::Change;
        default:
            if (!isdigit(char(key)) || _lastToken.type == TType::tt_pclose) {
                return ReturnState::None;
            }
            if (_firstUpdate) {
                _editBuffer.clear();
                _firstUpdate = false;
            }
            _editBuffer.push_back(char(key)); // add k to the string
            return ReturnState::Change;
        }
        return ReturnState::None;
    }
};

} // namespace DigitizerUi::components

#endif
