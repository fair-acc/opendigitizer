#include "Keypad.hpp"

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"
#include "StyledStandardWidgets.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <any>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <unordered_map>

namespace DigitizerUi::components {

std::vector<Token> tokenize(std::string_view text) {
    constexpr std::array<std::pair<std::string_view, TType>, 6UZ> kFunctions{{{"sinh(", TType::tt_sinh}, {"cosh(", TType::tt_cosh}, {"tanh(", TType::tt_tanh}, {"sin(", TType::tt_sin}, {"cos(", TType::tt_cos}, {"tan(", TType::tt_tan}}};
    const auto                                                    isNumberChar = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+' || c == 'e'; };

    std::vector<Token> tokens;
    std::size_t        pos  = 0UZ;
    const auto         emit = [&tokens, &pos, text](TType type, std::size_t first, std::size_t last) {
        tokens.push_back({type, text.substr(first, last - first)});
        pos = last;
    };
    const auto emitBinary = [&emit, &pos, text](TType type) {
        const std::size_t first = pos > 0UZ && text[pos - 1UZ] == ' ' ? pos - 1UZ : pos;
        const std::size_t last  = pos + 1UZ < text.size() && text[pos + 1UZ] == ' ' ? pos + 2UZ : pos + 1UZ;
        emit(type, first, last);
    };

    while (pos < text.size()) {
        const char c = text[pos];
        switch (c) {
        case ' ': ++pos; continue;
        case '+': emitBinary(TType::tt_plus); continue;
        case '*': emitBinary(TType::tt_mul); continue;
        case '/': emitBinary(TType::tt_div); continue;
        case '^': emitBinary(TType::tt_power); continue;
        case '-':
            if (pos + 1UZ == text.size() || text[pos + 1UZ] == ' ') {
                emitBinary(TType::tt_minus);
            } else {
                emit(TType::tt_uminus, pos, pos + 1UZ);
            }
            continue;
        case '(': emit(TType::tt_popen, pos, pos + 1UZ); continue;
        case ')': emit(TType::tt_pclose, pos, pos + 1UZ); continue;
        default: break;
        }

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            const auto end = std::ranges::find_if_not(text.substr(pos), isNumberChar);
            emit(TType::tt_const, pos, pos + static_cast<std::size_t>(end - text.substr(pos).begin()));
            continue;
        }
        if (const auto function = std::ranges::find_if(kFunctions, [rest = text.substr(pos)](const auto& entry) { return rest.starts_with(entry.first); }); function != kFunctions.end()) {
            emit(function->second, pos, pos + function->first.size());
            continue;
        }
        emit(TType::tt_none, pos, pos + 1UZ);
    }
    tokens.push_back({TType::tt_end, text.substr(text.size())});
    return tokens;
}

Token lastToken(std::string_view text) {
    const std::vector<Token> tokens = tokenize(text);
    return tokens.size() < 2UZ ? Token{} : tokens[tokens.size() - 2UZ];
}

bool onlyToken(std::string_view text) {
    const std::vector<Token> tokens    = tokenize(text);
    const std::size_t        signShift = !tokens.empty() && tokens.front().type == TType::tt_uminus ? 1UZ : 0UZ;
    return tokens.size() == 2UZ + signShift && tokens[signShift].type == TType::tt_const;
}

std::optional<float> parseNumber(std::string_view number) {
    float value = 0.f;
    if (const auto [_, ec] = std::from_chars(number.data(), number.data() + number.size(), value); ec != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

namespace {

struct ASTNode {
    TType type  = TType::tt_end;
    float value = 0.0f;
};

enum class Action : std::uint8_t {
    S, // shift           (<)
    R, // reduce          (>)
    E, // equal           (=)
    X, // nothing - error ( )
    A, // accept          (A)
};

[[nodiscard]] constexpr Action precedenceAction(TType stack, TType incoming) {
    using enum Action;
    // columns(incoming) {^}{-a}{*/}{+-}{f/(}{)}{id}{$}
    constexpr Action kPrecedence[][8] = {
        /* {^}    */ {S, S, R, R, S, R, S, R},
        /* {-a}   */ {R, X, R, R, S, R, S, R},
        /* {* }   */ {S, S, R, R, S, R, S, R},
        /* {+-}   */ {S, S, S, R, S, R, S, R},
        /* {f/(}  */ {S, S, S, S, S, E, S, X},
        /* {)}    */ {R, X, R, R, X, R, X, R},
        /* {$}    */ {S, S, S, S, S, X, S, A},
    };
    const auto tableIndex = [](TType entry) {
        switch (entry) {
        case TType::tt_power: return 0;
        case TType::tt_uminus: return 1;
        case TType::tt_mul:
        case TType::tt_div: return 2;
        case TType::tt_plus:
        case TType::tt_minus: return 3;
        case TType::tt_pclose: return 5;
        case TType::tt_end: return 6;
        default: return Token{entry, {}}.isPOpen() ? 4 : -1;
        }
    };

    const int row = tableIndex(stack);
    const int col = incoming == TType::tt_const ? 6 : incoming == TType::tt_end ? 7 : tableIndex(incoming);
    return row < 0 || col < 0 ? X : kPrecedence[row][col];
}

[[nodiscard]] bool reduce(std::vector<ASTNode>& context, std::size_t op) {
    const std::size_t size     = context.size();
    const auto        isExprAt = [&context](std::size_t index) { return context[index].type == TType::tt_expr; };
    switch (context[op].type) {
    case TType::tt_uminus:
        if (op + 2UZ != size || !isExprAt(op + 1UZ)) {
            return false;
        }
        context[op] = {TType::tt_expr, -context.back().value};
        context.pop_back();
        return true;
    case TType::tt_pclose: {
        if (op + 1UZ != size || size < 4UZ || !isExprAt(size - 2UZ)) {
            return false;
        }
        context.pop_back();
        const std::size_t open  = size - 3UZ;
        float&            value = context.back().value;
        switch (context[open].type) {
        case TType::tt_popen:
            context.erase(context.begin() + static_cast<std::ptrdiff_t>(open));
            if (context[open - 1UZ].type == TType::tt_uminus) {
                context.back().value = -context.back().value;
                context.erase(context.begin() + static_cast<std::ptrdiff_t>(open - 1UZ));
            }
            return true;
        case TType::tt_sin: value = sinf(value); break;
        case TType::tt_cos: value = cosf(value); break;
        case TType::tt_tan: value = tanf(value); break;
        case TType::tt_sinh: value = sinhf(value); break;
        case TType::tt_cosh: value = coshf(value); break;
        case TType::tt_tanh: value = tanhf(value); break;
        default: return false;
        }
        context.erase(context.begin() + static_cast<std::ptrdiff_t>(open));
        return true;
    }
    default: break;
    }

    if (op + 2UZ != size || op < 2UZ || !isExprAt(op - 1UZ) || !isExprAt(op + 1UZ)) {
        return false;
    }
    const float a    = context[op + 1UZ].value;
    const float b    = context[op - 1UZ].value;
    const TType type = context[op].type;
    context.pop_back();
    context.pop_back();
    switch (type) {
    case TType::tt_plus: context.back().value = a + b; return true;
    case TType::tt_minus: context.back().value = b - a; return true;
    case TType::tt_mul: context.back().value = b * a; return true;
    case TType::tt_div: context.back().value = b / a; return true;
    case TType::tt_power: context.back().value = powf(b, a); return true;
    default: return false;
    }
}

} // namespace

std::optional<float> evaluate(std::string_view text) {
    const std::vector<Token> tokens = tokenize(text);
    std::vector<ASTNode>     context(1UZ);

    for (std::size_t next = 0UZ; next < tokens.size();) {
        const Token&      incoming = tokens[next];
        const std::size_t op       = static_cast<std::size_t>(std::ranges::find_last_if(context, [](const ASTNode& node) { return node.type != TType::tt_expr; }).begin() - context.begin());

        switch (precedenceAction(context[op].type, incoming.type)) {
        case Action::E:
        case Action::S:
            if (incoming.type == TType::tt_const) {
                const std::optional<float> number = parseNumber(incoming.range);
                if (!number) {
                    return std::nullopt;
                }
                context.push_back({TType::tt_expr, *number});
            } else {
                context.push_back({incoming.type});
            }
            ++next;
            break;
        case Action::R:
            if (!reduce(context, op)) {
                return std::nullopt;
            }
            break;
        case Action::X: return std::nullopt;
        case Action::A:
            if (context.size() != 2UZ || context.back().type != TType::tt_expr) {
                return std::nullopt;
            }
            return context.back().value;
        }
    }
    return std::nullopt;
}

double incrementOf(std::string_view value, TrailingZeros trailingZeros) {
    if (value.empty()) {
        return 1.0;
    }
    const std::size_t dotPos = value.find('.');
    if (dotPos == std::string_view::npos) {
        const std::size_t firstNonZero = value.find_first_not_of('0');
        if (firstNonZero == std::string_view::npos) {
            return 1.0;
        }
        return std::pow(10.0, static_cast<int>(value.size() - firstNonZero) - 1);
    }
    const std::size_t lastDigit = trailingZeros == TrailingZeros::Ignored ? value.find_last_not_of('0') : value.size() - 1UZ;
    if (lastDigit <= dotPos) {
        return 1.0;
    }
    return std::pow(10.0, -static_cast<int>(lastDigit - dotPos));
}

int decimalPrecision(std::string_view value, TrailingZeros trailingZeros) {
    const std::size_t dotPos = value.find('.');
    if (dotPos == std::string_view::npos) {
        return 0;
    }
    const std::size_t lastDigit = trailingZeros == TrailingZeros::Ignored ? value.find_last_not_of('0') : value.size() - 1UZ;
    return lastDigit <= dotPos ? 0 : static_cast<int>(lastDigit - dotPos);
}

namespace {

struct KeypadState {
    static inline constexpr const char* keypad_name = "KeypadX";

    std::string                             _currentBlockUniqueName;
    std::unordered_map<std::string, double> _manualyEditedIncrements;
    std::unordered_map<std::string, int>    _manualyEditedPrecisions;

    //
    bool        _visible     = false;
    bool        _altMode     = false;
    bool        _invMode     = false;
    bool        _firstUpdate = true;
    size_t      _parentheses = 0;
    std::string _editBuffer;
    std::any    _prevValue;

    enum class ReturnState { None, Change, Accept, Discard };

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

        POpen  = '(',
        PClose = ')',

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
    [[nodiscard]] consteval static const char* toString(Button button) noexcept {
        using enum Button;
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
        default: static_assert(always_true<T>, "not all possible Button enum values are mapped");
        }
    }

    enum class LinePreference { SameLine, None };

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

        if (constexpr auto keyList = std::array{keyBinding...}; ImGui::IsKeyPressed(keyList[0])) {
            return primaryButton;
        } else if ((ImGui::IsKeyPressed(keyBinding) || ...)) {
            return secondaryButton;
        }

        if (buttonActivated) {
            static double lastClick = -1.0;
            static ImVec2 lastClickPos{-1.f, -1.f};
            const double  time                    = ImGui::GetTime();
            const ImVec2  clickPos                = ImGui::GetMousePos();
            const bool    isWithinDoubleClickTime = lastClick >= 0.0 && time - lastClick <= static_cast<double>(ImGui::GetIO().MouseDoubleClickTime);
            const auto    isDoubleClick           = [&]() { return (lastClickPos.x != -1 && std::hypot(clickPos.x - lastClickPos.x, clickPos.y - lastClickPos.y) <= ImGui::GetIO().MouseDoubleClickMaxDist); };
            bool          doubleClicked           = false;
            if (isWithinDoubleClickTime && isDoubleClick()) {
                doubleClicked = true;
            }
            lastClick    = time;
            lastClickPos = clickPos;
            return doubleClicked ? secondaryButton : primaryButton;
        }

        return oldValue;
    }

    static KeypadState& getInstance() {
        static KeypadState instance;
        return instance;
    }

    static constexpr const char* decrementButtonIcon = "\u{f146}";
    static constexpr const char* incrementButtonIcon = "\u{f0fe}";

    template<typename EdTy>
    [[nodiscard]] static bool drawChangeButtons(const std::string& key, const char* label, EdTy* value) {
        assert(label);
        auto& keyPad = getInstance();

        bool result = keyPad.editImpl(label, value);
        if (result) {
            const int precision = decimalPrecision(keyPad._editBuffer, TrailingZeros::Significant);
            keyPad._manualyEditedPrecisions.insert_or_assign(key, precision);

            const double increment = incrementOf(keyPad._editBuffer, TrailingZeros::Significant);
            keyPad._manualyEditedIncrements.insert_or_assign(key, increment);
        }

        const auto changeButton = [&keyPad, &key, value](const char* icon, const char* tooltip, bool decrement) {
            bool pressed = false;
            {
                ImGui::SameLine();
                IMW::Font _(LookAndFeel::instance().fontIconsSolid);
                if (DigitizerUi::Button(icon)) {
                    auto it = keyPad._manualyEditedIncrements.find(key);
                    if (it == keyPad._manualyEditedIncrements.end()) {
                        it = keyPad._manualyEditedIncrements.insert_or_assign(key, incrementOf(std::format("{:.4f}", static_cast<double>(*value)), TrailingZeros::Ignored)).first;
                    }
                    if (decrement) {
                        *value -= static_cast<EdTy>(it->second);
                    } else {
                        *value += static_cast<EdTy>(it->second);
                    }
                    pressed = true;
                }
            }
            IMW::detail::setItemTooltip(tooltip);
            return pressed;
        };
        result = changeButton(decrementButtonIcon, "Decrement", true) || result;
        result = changeButton(incrementButtonIcon, "Increment", false) || result;

        return result;
    }

    template<std::floating_point EdTy>
    [[nodiscard]] static std::string floatingPointDragFormat(const std::string& key, EdTy* value, std::string_view suffix) {
        auto&     keyPad    = getInstance();
        const int precision = [&keyPad, &key, value]() -> int {
            if (auto it = keyPad._manualyEditedPrecisions.find(key); it != keyPad._manualyEditedPrecisions.end()) {
                return it->second;
            } else {
                return decimalPrecision(std::format("{:.4f}", *value), TrailingZeros::Ignored);
            }
        }();

        return suffix.empty() ? std::format("%.{}f", precision) : std::format("%.{}f {}", precision, suffix);
    }

    static std::string intDragFormat(std::string_view suffix) { return suffix.empty() ? "%d" : std::format("%d {}", suffix); }

    static void clearIfNewBlock(std::string_view block) {
        auto& keyPad = getInstance();
        if (keyPad._currentBlockUniqueName != block) {
            keyPad._currentBlockUniqueName = block;
            keyPad._manualyEditedIncrements.clear();
            keyPad._manualyEditedPrecisions.clear();
        }
    }

    template<typename EdTy>
    [[nodiscard]] static bool edit(const std::string& key, const char* label, EdTy* value, std::string_view suffix = {}) {
        if (!label || !value) {
            return false;
        }

        constexpr bool hasChangeButtons = !std::same_as<std::string, EdTy>;

        const float totalWidth  = ImGui::CalcItemWidth();
        float       buttonWidth = 0.f;
        if constexpr (hasChangeButtons) {
            IMW::Font iconFont(LookAndFeel::instance().fontIconsSolid);
            buttonWidth = IMW::CalcButtonSize(decrementButtonIcon).x;
        }
        const float spacing                     = ImGui::GetStyle().ItemSpacing.x;
        const float spaceAvailableBeforeButtons = std::max(1.f, totalWidth - (hasChangeButtons ? 2.f * (buttonWidth + spacing) : 0.f));

        auto&      keyPad = getInstance();
        IMW::Group group;

        if constexpr (std::floating_point<EdTy>) {
            ImGuiDataType type = ImGuiDataType_Float;
            if constexpr (std::is_same_v<EdTy, double>) {
                type = ImGuiDataType_Double;
            } else {
                static_assert(std::is_same_v<EdTy, float>, "unsupported type");
            }

            EdTy min = 0;
            EdTy max = 0;
            ImGui::SetNextItemWidth(spaceAvailableBeforeButtons);
            const bool wasDragged = ImGui::DragScalar(label, type, value, 1.f, &min, &max, floatingPointDragFormat(key, value, suffix).c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                ImGui::SetKeyboardFocusHere(-1); // keyboard focus on drag input converts it to a text input
            }
            IMW::detail::setItemTooltip(key.c_str());

            const bool changedByKeypadOrButtons = drawChangeButtons(key, label, value);
            return wasDragged || changedByKeypadOrButtons;
        } else if constexpr (std::integral<EdTy>) {
            ImGuiDataType type = ImGuiDataType_S32;
            if constexpr (std::is_same_v<EdTy, std::int64_t>) {
                type = ImGuiDataType_S64;
            } else if constexpr (std::is_same_v<EdTy, std::uint64_t>) {
                type = ImGuiDataType_U64;
            } else if constexpr (std::is_same_v<EdTy, std::uint32_t>) {
                type = ImGuiDataType_U32;
            } else {
                static_assert(std::is_same_v<EdTy, std::int32_t>, "unsupported type");
            }

            EdTy min = 0;
            EdTy max = 0;
            ImGui::SetNextItemWidth(spaceAvailableBeforeButtons);
            const bool wasDragged = ImGui::DragScalar(label, type, value, 1.f, &min, &max, intDragFormat(suffix).c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                ImGui::SetKeyboardFocusHere(-1); // keyboard focus on drag input converts it to a text input
            }
            IMW::detail::setItemTooltip(key.c_str());

            const bool changedByKeypadOrButtons = drawChangeButtons(key, label, value);
            return wasDragged || changedByKeypadOrButtons;
        } else {
            ImGui::SetNextItemWidth(spaceAvailableBeforeButtons);
            ImGui::InputText(label, value);
            IMW::detail::setItemTooltip(key.c_str());

            return keyPad.editImpl(label, value);
        }
    }
    static bool isVisible() noexcept { return getInstance()._visible; }

    [[nodiscard]] ReturnState drawKeypadPopup(std::string& valueLabel) noexcept {
        const auto&  mainViewPort         = *ImGui::GetMainViewport();
        const ImVec2 mainViewPortSize     = mainViewPort.WorkSize;
        const bool   portraitMode         = mainViewPortSize.x < mainViewPortSize.y;
        const ImVec2 defaultPortraitSize  = LookAndFeel::instance().keypadPortraitSize;
        const ImVec2 defaultLandscapeSize = LookAndFeel::instance().keypadLandscapeSize;

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
                IMW::StyleFloatVar style(ImGuiStyleVar_FrameRounding, LookAndFeel::instance().keypadRounding);
                IMW::Font          font(LookAndFeel::instance().fontBigger());
                const Button       activatedKey = windowSize.x < windowSize.y ? drawPortraitKeypad(valueLabel, windowSize) : drawLandscapeKeypad(valueLabel, windowSize);
                returnState                     = processKeypadLogic(activatedKey);
            }

            switch (returnState) {
            case ReturnState::Change: _firstUpdate = false; return ReturnState::Change;
            case ReturnState::Accept: _firstUpdate = true; return ReturnState::Accept;
            case ReturnState::Discard: _firstUpdate = true; return ReturnState::Discard;
            default: return ReturnState::None;
            }
        }
        return ReturnState::None;
    }

    template<typename EdTy>
    [[nodiscard]] bool editImpl(const char* /* label */, EdTy* value) noexcept {
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::TempInputIsActive(ImGui::GetItemID())) {
            _visible = true;
            ImGui::OpenPopup(keypad_name);
            _prevValue.emplace<EdTy>(*value); // save for discard
            _editBuffer.clear();
            std::format_to(std::back_inserter(_editBuffer), "{}", *value);
        }

        switch (drawKeypadPopup(_editBuffer)) {
        case ReturnState::Accept:
            if constexpr (std::same_as<std::string, EdTy>) {
                *value = _editBuffer;
            } else if constexpr (std::floating_point<EdTy>) {
                if constexpr (std::same_as<float, EdTy>) {
                    *value = std::strtof(_editBuffer.c_str(), nullptr);
                } else if constexpr (std::same_as<double, EdTy>) {
                    *value = std::strtod(_editBuffer.c_str(), nullptr);
                } else {
                    *value = std::strtold(_editBuffer.c_str(), nullptr);
                }
            } else {
                EdTy             converted = 0;
                std::string_view a{_editBuffer};
                auto [ptr, ec] = std::from_chars(a.data(), a.data() + a.size(), converted);
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
        default: return false;
        }
    }

    [[nodiscard]] Button drawPortraitKeypad(std::string& valueLabel, const ImVec2& windowSize) const {
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
        using enum Button;
        using enum LinePreference;
        const ImGuiStyle& style = ImGui::GetStyle();
        const float       nx    = floorf(windowSize.x / nCols) - .5f * nCols / (nCols - 1) * style.WindowPadding.x;
        const float       ny    = floorf(windowSize.y / nRows) - .5f * nRows / (nRows - 1) * style.WindowPadding.y;
        ImVec2            buttonSize(std::min(nx, ny), std::min(nx, ny)); // buttons are square
        Button            key = Button::NoButton;                         // return value

        // setup common number editing field
        {
            IMW::Font font(LookAndFeel::instance().fontLarge());
            // the '-1' is to accommodate the 'Esc' button
            IMW::ItemWidth itemWidth(buttonSize.x * (nCols - 1.f) + (nCols - 2.f) * style.WindowPadding.x);
            if (ImGui::InputText("##hidden", &valueLabel, ImGuiInputTextFlags_CharsScientific)) {
                return NoButton;
            }
        }

        // the 'ESC' clause button
        {
            IMW::StyleColor button(ImGuiCol_Button, LookAndFeel::instance().palette().keypadButtonColor);
            IMW::StyleColor text(ImGuiCol_Text, LookAndFeel::instance().palette().keypadTextColor);
            key = keypadButton<SameLine, Escape, ImGuiKey_Escape>(buttonSize, key);
        }

        // start row 2: 2nd,sin[h],cos[h],tan[h],<-
        if (_altMode) {
            ImVec4 button_color = style.Colors[ImGuiCol_Button];
            button_color.x *= LookAndFeel::instance().palette().keypadFunctionKeyButtonColorMultiplier.x;
            button_color.y *= LookAndFeel::instance().palette().keypadFunctionKeyButtonColorMultiplier.y;
            button_color.z *= LookAndFeel::instance().palette().keypadFunctionKeyButtonColorMultiplier.z;

            IMW::StyleColor button(ImGuiCol_Button, button_color);
            IMW::StyleColor text(ImGuiCol_Text, LookAndFeel::instance().palette().keypadTextColor);
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
            button_color.x *= LookAndFeel::instance().palette().keypadFunctionKeyButtonColorMultiplier.x;
            button_color.y *= LookAndFeel::instance().palette().keypadFunctionKeyButtonColorMultiplier.y;
            button_color.z *= LookAndFeel::instance().palette().keypadFunctionKeyButtonColorMultiplier.z;
            IMW::StyleColor button(ImGuiCol_Button, button_color);
            IMW::StyleColor text(ImGuiCol_Text, LookAndFeel::instance().palette().keypadTextColor);
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
        key                          = keypadButton<SameLine, Add, ImGuiKey_KeypadAdd>({buttonSize.x, buttonSize.y * 2.0f + 0.5f * style.WindowPadding.y}, key);
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
            IMW::StyleColor button(ImGuiCol_Button, LookAndFeel::instance().palette().keypadButtonColor);
            IMW::StyleColor text(ImGuiCol_Text, LookAndFeel::instance().palette().keypadTextColor);
            const float     vpos_before_enter = ImGui::GetCursorPosY();
            key                               = keypadButton<SameLine, Enter, ImGuiKey_Enter, ImGuiKey_KeypadEnter>({buttonSize.x, buttonSize.y * 2.f + 0.5f * style.WindowPadding.y}, key);
            ImGui::SetCursorPosY(vpos_before_enter);
        }

        // start row 8: ±,0 (two columns),.,enter(2/2, two rows)
        key = keypadButton<None, Sign>(buttonSize, key);
        key = keypadButton<SameLine, Button0, ImGuiKey_0, ImGuiKey_Keypad0>({buttonSize[0] * 2.f + style.WindowPadding.x, buttonSize.y}, key);
        key = keypadButton<SameLine, Period, ImGuiKey_Period, ImGuiKey_KeypadDecimal>(buttonSize, key);
        return key;
    }

    [[nodiscard]] Button drawLandscapeKeypad(std::string& valueLabel, const ImVec2& windowSize) const {
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
        using enum Button;
        using enum LinePreference;
        const ImGuiStyle& style = ImGui::GetStyle();
        const float       nx    = floorf(windowSize.x / nCols) - .5f * nCols / (nCols - 1) * style.WindowPadding.x;
        const float       ny    = floorf(windowSize.y / nRows) - .5f * nRows / (nRows - 1) * style.WindowPadding.y;
        ImVec2            buttonSize(std::min(nx, ny), std::min(nx, ny)); // buttons are square
        Button            key = Button::NoButton;                         // return value

        // setup common number editing field
        {
            IMW::Font font(LookAndFeel::instance().fontLarge());
            // the '-2' is to accommodate the '<-' and 'Esc' button
            IMW::ItemWidth itemWidth(buttonSize.x * (nCols - 2.f) + (nCols - 3.f) * style.WindowPadding.x);
            if (ImGui::InputText("##hidden", &valueLabel, ImGuiInputTextFlags_CharsScientific)) {
                return NoButton;
            }
        }

        // the '<-' button is exceptionally placed next to the 'ESC' button
        key = keypadButton<SameLine, Backspace, AC /* double-click action  */, ImGuiKey_Backspace, ImGuiKey_Delete>(buttonSize, key);

        // the 'ESC' clause button
        {
            IMW::StyleColor button(ImGuiCol_Button, LookAndFeel::instance().palette().keypadButtonColor);
            IMW::StyleColor text(ImGuiCol_Text, LookAndFeel::instance().palette().keypadTextColor);
            key = keypadButton<SameLine, Escape, ImGuiKey_Escape>(buttonSize, key);
        }

        // Row 2: 2nd,Inv,Log,10^x,/,*,-
        if (_altMode) {
            ImVec4 button_color = style.Colors[ImGuiCol_Button];
            button_color.x *= LookAndFeel::instance().palette().keypadFunctionKeyButtonColorMultiplier.x;
            button_color.y *= LookAndFeel::instance().palette().keypadFunctionKeyButtonColorMultiplier.y;
            button_color.z *= LookAndFeel::instance().palette().keypadFunctionKeyButtonColorMultiplier.z;
            IMW::StyleColor button(ImGuiCol_Button, button_color);
            IMW::StyleColor text(ImGuiCol_Text, LookAndFeel::instance().palette().keypadTextColor);
            key = keypadButton<None, Alt_2nd, ImGuiKey_NumLock>(buttonSize, key);
        } else {
            key = keypadButton<None, Alt_2nd, ImGuiKey_NumLock>(buttonSize, key);
        }
        if (_invMode) {
            ImVec4 button_color = style.Colors[ImGuiCol_Button];
            button_color.x *= LookAndFeel::instance().palette().keypadFunctionKeyButtonColorMultiplier.x;
            button_color.y *= LookAndFeel::instance().palette().keypadFunctionKeyButtonColorMultiplier.y;
            button_color.z *= LookAndFeel::instance().palette().keypadFunctionKeyButtonColorMultiplier.z;
            IMW::StyleColor button(ImGuiCol_Button, button_color);
            IMW::StyleColor text(ImGuiCol_Text, LookAndFeel::instance().palette().keypadTextColor);
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
        key                          = keypadButton<SameLine, Add, ImGuiKey_KeypadAdd>({buttonSize.x, buttonSize.y * 2.0f + 0.5f * style.WindowPadding.y}, key);
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
            IMW::StyleColor button(ImGuiCol_Button, LookAndFeel::instance().palette().keypadButtonColor);
            IMW::StyleColor text(ImGuiCol_Text, LookAndFeel::instance().palette().keypadTextColor);
            const float     vpos_before_enter = ImGui::GetCursorPosY();
            key                               = keypadButton<SameLine, Enter, ImGuiKey_Enter, ImGuiKey_KeypadEnter>({buttonSize.x, buttonSize.y * 2.f + 0.5f * style.WindowPadding.y}, key);
            ImGui::SetCursorPosY(vpos_before_enter);
        }

        // Row 7: ?,^,±,0,0,.,enter(2/2, two rows)
        key = keypadButton<None, NoButton>(buttonSize, key);
        key = keypadButton<SameLine, Pow>(buttonSize, key);
        key = keypadButton<SameLine, Sign>(buttonSize, key);
        key = keypadButton<SameLine, Button0, ImGuiKey_0, ImGuiKey_Keypad0>({buttonSize[0] * 2.f + style.WindowPadding.x, buttonSize.y}, key);
        key = keypadButton<SameLine, Period, ImGuiKey_Period, ImGuiKey_KeypadDecimal>(buttonSize, key);

        return key;
    }

    [[nodiscard]] ReturnState processKeypadLogic(const Button& key) {
        using enum Button;
        const Token last = lastToken(_editBuffer);

        switch (key) {
        case NoButton: return ReturnState::None;
        case Escape: return ReturnState::Discard;
        case Enter: {
            if (last.type != TType::tt_const && last.type != TType::tt_pclose) {
                return ReturnState::None;
            }
            if (onlyToken(_editBuffer)) {
                return ReturnState::Accept;
            }
            auto result = evaluate(_editBuffer);
            if (!result) {
                return ReturnState::None;
            }
            _editBuffer = std::format("{}", result.value());
            return ReturnState::Change;
        }

        case Backspace:
            if (last.type == TType::tt_const) {
                _editBuffer.pop_back();
            } else {
                _editBuffer.erase(std::prev(_editBuffer.end(), static_cast<std::ptrdiff_t>(last.range.size())), _editBuffer.end());
            }

            if (last.range.data() != _editBuffer.data() && !_editBuffer.empty() && _editBuffer.back() == '-') {
                _editBuffer.pop_back();
            }
            return ReturnState::Change;
        case AC: _editBuffer.clear(); return ReturnState::Change;
        case Alt_2nd: _altMode = !_altMode; return ReturnState::Change;
        case Alt_Inv: _invMode = !_invMode; return ReturnState::Change;

        case Sign: {
            if (last.type == TType::tt_const) {
                if (last.range.data() != _editBuffer.data() && last.range.data()[-1] == '-') {
                    _editBuffer.erase(std::prev(_editBuffer.end(), static_cast<std::ptrdiff_t>(last.range.size() + 1UZ)));
                } else {
                    _editBuffer.insert(std::prev(_editBuffer.end(), static_cast<std::ptrdiff_t>(last.range.size())), '-');
                }
                return ReturnState::Change;
            }

            if (last.type != TType::tt_pclose) {
                return ReturnState::None;
            }

            const std::vector<Token> tokens = tokenize(_editBuffer);
            std::size_t              depth  = 0UZ;
            const auto               open   = std::find_if(tokens.rbegin(), tokens.rend(), [&depth](const Token& token) {
                if (token.type == TType::tt_pclose) {
                    ++depth;
                } else if (token.isPOpen() && depth > 0UZ) {
                    return --depth == 0UZ;
                }
                return false;
            });
            if (open == tokens.rend()) {
                return ReturnState::None;
            }

            const auto iter = _editBuffer.begin() + (open->range.data() - _editBuffer.data());
            if (iter != _editBuffer.begin() && *(iter - 1) == '-') {
                _editBuffer.erase(iter - 1);
            } else {
                _editBuffer.insert(iter, '-');
            }

            return ReturnState::Change;
        }
        case Sqrt:
            if (last.type != TType::tt_const) {
                return ReturnState::None;
            }
            {
                const std::optional<float> number = parseNumber(last.range);
                if (!number) {
                    return ReturnState::None;
                }
                const float f = *number;
                if (f < 0.0f) {
                    return ReturnState::None;
                }
                _editBuffer.erase(std::prev(_editBuffer.end(), static_cast<std::ptrdiff_t>(last.range.size())), _editBuffer.end());
                _editBuffer.append(std::format("{}", sqrtf(f)));
            }
            return ReturnState::Change;
        case Rcp: // reciprocate
            if (last.type != TType::tt_const) {
                return ReturnState::None;
            }
            {
                const std::optional<float> number = parseNumber(last.range);
                if (!number) {
                    return ReturnState::None;
                }
                const float f = *number;
                if (f == 0.0f) {
                    return ReturnState::None;
                }
                _editBuffer.erase(std::prev(_editBuffer.end(), static_cast<std::ptrdiff_t>(last.range.size())), _editBuffer.end());
                _editBuffer.append(std::format("{}", 1.0f / f));
            }
            return ReturnState::Change;
        case Percent:
            if (last.type != TType::tt_const) {
                return ReturnState::None;
            }
            {
                const std::optional<float> number = parseNumber(last.range);
                if (!number) {
                    return ReturnState::None;
                }
                const float f = *number;
                if (f == 0.0f) {
                    return ReturnState::None;
                }
                _editBuffer.erase(std::prev(_editBuffer.end(), static_cast<std::ptrdiff_t>(last.range.size())), _editBuffer.end());
                _editBuffer.append(std::format("{}", f / 100.0f));
            }
            return ReturnState::Change;
        case Period:
            if (last.type != TType::tt_const || last.range.find('.') != std::string_view::npos) {
                break;
            }
            _editBuffer.push_back('.'); // add period to the string
            return ReturnState::Change;
        case E_scientific:
            if (last.type != TType::tt_const || last.range.find('.') != std::string_view::npos) {
                break;
            }
            _editBuffer.push_back('e'); // add exponential 'e' to the string
            return ReturnState::Change;
        case Add:
        case Sub:
        case Mul:
        case Div:
        case Pow:
            if (!last.isValid() || last.isPOpen()) {
                return ReturnState::None;
            }
            if (last.isOperator()) {
                _editBuffer.replace(static_cast<std::size_t>(last.range.data() - _editBuffer.data()), last.range.size(), std::string{' ', char(key), ' '});
                return ReturnState::Change;
            }

            _editBuffer.push_back(' ');
            _editBuffer.push_back(char(key));
            _editBuffer.push_back(' ');
            return ReturnState::Change;
        case POpen:
            if (last.isValid() && (!last.isPOpen() && !last.isOperator())) {
                return ReturnState::None;
            }

            _parentheses++;
            _editBuffer.push_back(char(key));
            return ReturnState::Change;
        case PClose:
            if (!last.isValid() || last.isPOpen() || last.isOperator() || !_parentheses) {
                return ReturnState::None;
            }

            _parentheses--;
            _editBuffer.push_back(char(key));
            return ReturnState::Change;
        case Sin:
            if (last.isValid() && (!last.isPOpen() && !last.isOperator())) {
                return ReturnState::None;
            }
            _editBuffer.append("sin(");
            _parentheses++;
            return ReturnState::Change;
        case Sinh:
            if (last.isValid() && (!last.isPOpen() && !last.isOperator())) {
                return ReturnState::None;
            }
            _editBuffer.append("asinh(");
            _parentheses++;
            return ReturnState::Change;
        case ASin:
            if (last.isValid() && (!last.isPOpen() && !last.isOperator())) {
                return ReturnState::None;
            }
            _editBuffer.append("asin(");
            _parentheses++;
            return ReturnState::Change;
        case ASinh:
            if (last.isValid() && (!last.isPOpen() && !last.isOperator())) {
                return ReturnState::None;
            }
            _editBuffer.append("sinh(");
            _parentheses++;
            return ReturnState::Change;
        case Cos:
            if (last.isValid() && (!last.isPOpen() && !last.isOperator())) {
                return ReturnState::None;
            }
            _editBuffer.append("cos(");
            _parentheses++;
            return ReturnState::Change;
        case Cosh:
            if (last.isValid() && (!last.isPOpen() && !last.isOperator())) {
                return ReturnState::None;
            }
            _editBuffer.append("cosh(");
            _parentheses++;
            return ReturnState::Change;
        case ACos:
            if (last.isValid() && (!last.isPOpen() && !last.isOperator())) {
                return ReturnState::None;
            }
            _editBuffer.append("acos(");
            _parentheses++;
            return ReturnState::Change;
        case ACosh:
            if (last.isValid() && (!last.isPOpen() && !last.isOperator())) {
                return ReturnState::None;
            }
            _editBuffer.append("acosh(");
            _parentheses++;
            return ReturnState::Change;
        case Tan:
            if (last.isValid() && (!last.isPOpen() && !last.isOperator())) {
                return ReturnState::None;
            }
            _editBuffer.append("tan(");
            _parentheses++;
            return ReturnState::Change;
        case Tanh:
            if (last.isValid() && (!last.isPOpen() && !last.isOperator())) {
                return ReturnState::None;
            }
            _editBuffer.append("tanh(");
            _parentheses++;
            return ReturnState::Change;
        case ATan:
            if (last.isValid() && (!last.isPOpen() && !last.isOperator())) {
                return ReturnState::None;
            }
            _editBuffer.append("atan(");
            _parentheses++;
            return ReturnState::Change;
        case ATanh:
            if (last.isValid() && (!last.isPOpen() && !last.isOperator())) {
                return ReturnState::None;
            }
            _editBuffer.append("atanh(");
            _parentheses++;
            return ReturnState::Change;
        default:
            if (!isdigit(char(key)) || last.type == TType::tt_pclose) {
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

} // namespace

bool InputKeypad::edit(const std::string& key, const char* label, std::int32_t* value, std::string_view suffix) { return KeypadState::edit(key, label, value, suffix); }
bool InputKeypad::edit(const std::string& key, const char* label, std::uint32_t* value, std::string_view suffix) { return KeypadState::edit(key, label, value, suffix); }
bool InputKeypad::edit(const std::string& key, const char* label, std::int64_t* value, std::string_view suffix) { return KeypadState::edit(key, label, value, suffix); }
bool InputKeypad::edit(const std::string& key, const char* label, std::uint64_t* value, std::string_view suffix) { return KeypadState::edit(key, label, value, suffix); }
bool InputKeypad::edit(const std::string& key, const char* label, float* value, std::string_view suffix) { return KeypadState::edit(key, label, value, suffix); }
bool InputKeypad::edit(const std::string& key, const char* label, double* value, std::string_view suffix) { return KeypadState::edit(key, label, value, suffix); }
bool InputKeypad::edit(const std::string& key, const char* label, std::string* value, std::string_view suffix) { return KeypadState::edit(key, label, value, suffix); }

void InputKeypad::clearIfNewBlock(std::string_view block) { KeypadState::clearIfNewBlock(block); }

bool InputKeypad::isVisible() noexcept { return KeypadState::isVisible(); }

} // namespace DigitizerUi::components
