#ifndef IMPLOT_POINT_CLASS_EXTRA
#define IMGUI_DEFINE_MATH_OPERATORS true
#endif

#include <any>
#include <cctype>
#include <charconv>
#include <fmt/format.h>
#include <functional>
#include <imgui_internal.h>
#include <tuple>
#include <type_traits>

#include "app.h"
#include "calculator.h"
#include "flowgraph.h"
#include "flowgraph/datasink.h"
#include "imguiutils.h"

namespace ImGuiUtils {

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

    template<LinePreference SameLine, Button primaryButton, ImGuiKey_... keyBinding>
    [[nodiscard]] Button static keypadButton(ImVec2 size, Button oldValue) {
        if constexpr (SameLine == LinePreference::SameLine) {
            ImGui::SameLine();
        }
        return ImGui::Button(toString(primaryButton), size) || (ImGui::IsKeyPressed(keyBinding) || ...) ? primaryButton : oldValue;
    }
    template<LinePreference SameLine, Button primaryButton, Button secondaryButton, ImGuiKey_... keyBinding>
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

        if (ImGui::BeginPopupModal(keypad_name, &_visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration)) {
            struct PopupGuard {
                ~PopupGuard() { ImGui::EndPopup(); }
            } guard;

            ReturnState returnState = ReturnState::None;
            if (ImGui::BeginChild("drawKeypad Input", ImVec2{}, true)) {
                const ImVec2 windowSize = ImGui::GetContentRegionAvail(); // now inside this child;

                // setup number and function keypad -- global style settings
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6);
                ImGui::PushFont(DigitizerUi::App::instance().fontBigger[DigitizerUi::App::instance().prototypeMode]);
                const Button activatedKey = windowSize.x < windowSize.y ? drawPortraitKeypad(valueLabel, windowSize) : drawLandscapeKeypad(valueLabel, windowSize);
                ImGui::PopFont();
                ImGui::PopStyleVar();

                ImGui::EndChild();
                returnState = processKeypadLogic(activatedKey);
            }

            switch (returnState) {
            case ReturnState::Change:
                _firstUpdate = false;
                _lastToken   = ::last_token(_editBuffer);
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
            _lastToken = ::last_token(_editBuffer);
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
        ImGui::PushFont(DigitizerUi::App::instance().fontLarge[DigitizerUi::App::instance().prototypeMode]);
        // the '-1' is to accommodate the 'Esc' button
        ImGui::PushItemWidth(buttonSize.x * (nCols - 1.f) + (nCols - 2.f) * style.WindowPadding.x);
        if (ImGui::InputText("##hidden", buffer.data(), buffer.size(), ImGuiInputTextFlags_CharsScientific)) {
            valueLabel = std::string(buffer.data());
            ImGui::PopFont();
            ImGui::PopItemWidth();
            return NoButton;
        }
        ImGui::PopFont();
        ImGui::PopItemWidth();

        // the 'ESC' clause button
        ImGui::PushStyleColor(ImGuiCol_Button, { 11.f / 255.f, 89.f / 255.f, 191.f / 255.f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 1.0f, 1.0f, 1.0f });
        key = keypadButton<SameLine, Escape, ImGuiKey_Escape>(buttonSize, key);
        ImGui::PopStyleColor(2);

        // start row 2: 2nd,sin[h],cos[h],tan[h],<-
        if (_altMode) {
            ImVec4 button_color = style.Colors[ImGuiCol_Button];
            button_color.x *= 0.6f; // R
            button_color.y *= 0.6f; // G
            button_color.z *= 0.8f; // B
            ImGui::PushStyleColor(ImGuiCol_Button, button_color);
            ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 1.0f, 1.0f, 1.0f });
            key = keypadButton<None, Alt_2nd, ImGuiKey_NumLock>(buttonSize, key);
            ImGui::PopStyleColor(2);
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
            ImGui::PushStyleColor(ImGuiCol_Button, button_color);
            ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 1.0f, 1.0f, 1.0f });
            key = keypadButton<None, Alt_Inv, ImGuiKey_CapsLock>(buttonSize, key);
            ImGui::PopStyleColor(2);
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
        ImGui::PushStyleColor(ImGuiCol_Button, { 11.f / 255.f, 89.f / 255.f, 191.f / 255.f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 1.0f, 1.0f, 1.0f });
        const float vpos_before_enter = ImGui::GetCursorPosY();
        key                           = keypadButton<SameLine, Enter, ImGuiKey_Enter, ImGuiKey_KeypadEnter>({ buttonSize.x, buttonSize.y * 2.f + 0.5f * style.WindowPadding.y }, key);
        ImGui::PopStyleColor(2);
        ImGui::SetCursorPosY(vpos_before_enter);

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
        ImGui::PushFont(DigitizerUi::App::instance().fontLarge[DigitizerUi::App::instance().prototypeMode]);
        // the '-2' is to accommodate the '<-' and 'Esc' button
        ImGui::PushItemWidth(buttonSize.x * (nCols - 2.f) + (nCols - 3.f) * style.WindowPadding.x);
        if (ImGui::InputText("##hidden", buffer.data(), buffer.size(), ImGuiInputTextFlags_CharsScientific)) {
            valueLabel = std::string(buffer.data());
            ImGui::PopFont();
            ImGui::PopStyleVar();
            ImGui::PopItemWidth();
            return NoButton;
        }
        ImGui::PopFont();
        ImGui::PopItemWidth();

        // the '<-' button is exceptionally placed next to the 'ESC' button
        key = keypadButton<SameLine, Backspace, AC /* double-click action  */, ImGuiKey_Backspace, ImGuiKey_Delete>(buttonSize, key);

        // the 'ESC' clause button
        ImGui::PushStyleColor(ImGuiCol_Button, { 11.f / 255.f, 89.f / 255.f, 191.f / 255.f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 1.0f, 1.0f, 1.0f });
        key = keypadButton<SameLine, Escape, ImGuiKey_Escape>(buttonSize, key);
        ImGui::PopStyleColor(2);

        // Row 2: 2nd,Inv,Log,10^x,/,*,-
        if (_altMode) {
            ImVec4 button_color = style.Colors[ImGuiCol_Button];
            button_color.x *= 0.6f; // R
            button_color.y *= 0.6f; // G
            button_color.z *= 0.8f; // B
            ImGui::PushStyleColor(ImGuiCol_Button, button_color);
            ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 1.0f, 1.0f, 1.0f });
            key = keypadButton<None, Alt_2nd, ImGuiKey_NumLock>(buttonSize, key);
            ImGui::PopStyleColor(2);
        } else {
            key = keypadButton<None, Alt_2nd, ImGuiKey_NumLock>(buttonSize, key);
        }
        if (_invMode) {
            ImVec4 button_color = style.Colors[ImGuiCol_Button];
            button_color.x *= 0.6f; // R
            button_color.y *= 0.6f; // G
            button_color.z *= 0.8f; // B
            ImGui::PushStyleColor(ImGuiCol_Button, button_color);
            ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 1.0f, 1.0f, 1.0f });
            key = keypadButton<SameLine, Alt_Inv, ImGuiKey_CapsLock>(buttonSize, key);
            ImGui::PopStyleColor(2);
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
        ImGui::PushStyleColor(ImGuiCol_Button, { 11.f / 255.f, 89.f / 255.f, 191.f / 255.f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 1.0f, 1.0f, 1.0f });
        const float vpos_before_enter = ImGui::GetCursorPosY();
        key                           = keypadButton<SameLine, Enter, ImGuiKey_Enter, ImGuiKey_KeypadEnter>({ buttonSize.x, buttonSize.y * 2.f + 0.5f * style.WindowPadding.y }, key);
        ImGui::PopStyleColor(2);
        ImGui::SetCursorPosY(vpos_before_enter);

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
}; // end: class InputKeypad

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
                    || InputKeypad<>::isVisible()) {
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
                    // "go up" buttons: for each output of the current block, and for each connection they have, put an arrow up button
                    // that switches to the connected block
                    for (auto &out : ctx.block->outputs()) {
                        for (const auto *conn : out.connections) {
                            ImGui::PushID(id++);

                            ImGui::PushFont(app.fontIconsSolid);
                            if (ImGui::Button(prevString, buttonSize)) {
                                ctx.block = conn->dst.block;
                            }
                            ImGui::PopFont();
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", conn->dst.block->name.c_str());
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
                                        ctx.insertFrom      = &conn->src.block->outputs()[conn->src.index];
                                        ctx.insertBefore    = &conn->dst.block->inputs()[conn->dst.index];
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
                                auto text = fmt::format("Before block '{}'", conn->dst.block->name);
                                if (ImGui::Selectable(text.c_str())) {
                                    ctx.insertBefore    = &conn->dst.block->inputs()[conn->dst.index];
                                    ctx.mode            = BlockControlsPanel::Mode::Insert;
                                    ctx.insertFrom      = &conn->src.block->outputs()[conn->src.index];
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
                        ctx.block = in.connections.front()->src.block;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::PopFont();
                        ImGui::SetTooltip("%s", in.connections.front()->src.block->name.c_str());
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

namespace {
template<class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template<typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;
} // namespace

void blockParametersControls(DigitizerUi::Block *b, bool verticalLayout, const ImVec2 &size) {
    const auto availableSize = ImGui::GetContentRegionAvail();

    auto       storage       = ImGui::GetStateStorage();
    ImGui::PushID("block_controls");

    const auto &style     = ImGui::GetStyle();
    const auto  indent    = style.IndentSpacing;
    const auto  textColor = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_Text]);

    int         i         = 0;
    for (const auto &p : b->parameters()) {
        auto id = ImGui::GetID(p.first.c_str());
        ImGui::PushID(int(id));
        auto *enabled = storage->GetBoolRef(id, true);

        ImGui::BeginGroup();
        const auto curpos = ImGui::GetCursorPos();

        ImGui::BeginGroup();

        bool controlDrawn = true;

        if (*enabled) {
            char label[64];
            snprintf(label, sizeof(label), "##parameter_%d", i);

            controlDrawn = std::visit(overloaded{
                                              [&](float val) {
                                                  ImGui::SetCursorPosY(curpos.y + ImGui::GetFrameHeightWithSpacing());
                                                  ImGui::SetNextItemWidth(100);
                                                  if (InputKeypad<>::edit(label, &val)) {
                                                      b->setParameter(p.first, val);
                                                      b->update();
                                                  }
                                                  return true;
                                              },
                                              // [&](std::string_view val) {
                                              //     ImGui::SetCursorPosY(curpos.y + ImGui::GetFrameHeightWithSpacing());
                                              //     ImGui::SetNextItemWidth(100);
                                              //
                                              //     std::string str(val);
                                              //     if (InputKeypad<>::edit(label, &str)) {
                                              //         b->setParameter(p.first, std::move(str));
                                              //         b->update();
                                              //     }
                                              //     return true;
                                              // },
                                              [&](auto &&val) {
                                                  using T = std::decay_t<decltype(val)>;
                                                  if constexpr (std::integral<T>) {
                                                      int v = val;
                                                      ImGui::SetCursorPosY(curpos.y + ImGui::GetFrameHeightWithSpacing());
                                                      ImGui::SetNextItemWidth(100);
                                                      if (InputKeypad<>::edit(label, &v)) {
                                                          b->setParameter(p.first, v);
                                                          b->update();
                                                      }
                                                      return true;
                                                  } else if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view>) {
                                                      ImGui::SetCursorPosY(curpos.y + ImGui::GetFrameHeightWithSpacing());
                                                      ImGui::SetNextItemWidth(100);

                                                      std::string str(val);
                                                      ImGui::InputText("##in", &str);
                                                      b->setParameter(p.first, std::move(str));
                                                      return true;
                                                  }
                                                  return false;
                                              } },
                    p.second);

            if (!controlDrawn) continue;

            /*
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
                            }*/
        }

        ImGui::EndGroup();
        ImGui::SameLine(0, 0);

        auto        width = verticalLayout ? availableSize.x : ImGui::GetCursorPosX() - curpos.x;
        const auto *text  = *enabled || verticalLayout ? p.first.c_str() : "";
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

        setItemTooltip("%s", p.first.c_str());

        ImGui::SetCursorPos(curpos + ImVec2(style.FramePadding.x, style.FramePadding.y));
        ImGui::RenderArrow(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), textColor, *enabled ? ImGuiDir_Down : ImGuiDir_Right, 1.0f);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
        if (*enabled || verticalLayout) {
            ImGui::TextUnformatted(p.first.c_str());
        }

        ImGui::EndGroup();

        if (!verticalLayout) {
            ImGui::SameLine();
        }

        ImGui::PopID();
        ++i;
    }
    ImGui::PopID();
}

} // namespace ImGuiUtils
