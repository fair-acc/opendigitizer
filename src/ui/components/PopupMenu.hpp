#ifndef OPENDIGITIZER_UI_COMPONENTS_POPUP_MENU_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_POPUP_MENU_HPP_

#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "../common/ImguiWrap.hpp"

namespace DigitizerUi {

namespace detail {
inline ImVec4 lightenColor(const ImVec4& color, float percent) {
    float h;
    float s;
    float v;
    ImGui::ColorConvertRGBtoHSV(color.x, color.y, color.z, h, s, v);
    s = std::max(0.0f, s * percent);
    float r;
    float g;
    float b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return {r, g, b, color.w};
}

inline ImVec4 darkenColor(const ImVec4& color, float percent) {
    float h;
    float s;
    float v;
    ImGui::ColorConvertRGBtoHSV(color.x, color.y, color.z, h, s, v);
    v = std::max(0.0f, v * percent);
    float r;
    float g;
    float b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return {r, g, b, color.w};
}
} // namespace detail

struct MenuButton {
    using CallbackFun = std::variant<std::function<void()>, std::function<void(MenuButton&)>>;
    std::string   label;
    std::string   optionalLabel;
    mutable float _size;
    CallbackFun   onClick;
    ImFont*       font = nullptr;
    std::string   toolTip;
    bool          isTransparent = false;
    bool          isNewRow      = false;
    float         padding       = std::max(ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y);
    ImVec4        buttonColor   = ImGui::GetStyleColorVec4(ImGuiCol_Button);

    [[nodiscard]] float size() const {
        IMW::Font    _(font);
        const ImVec2 textSize         = ImGui::CalcTextSize(label.c_str());
        const float  maxSize          = std::max(textSize.x, textSize.y);
        const float  actualButtonSize = std::max(_size, 2.f * padding + maxSize);
        _size                         = actualButtonSize;
        return _size;
    }

    [[nodiscard]] bool create(float buttonRounding = -1.f) {
        const std::string buttonId  = fmt::format("#{}", label);
        bool              isClicked = false;
        {
            const float        actualButtonSize = size();
            IMW::Font          _(font);
            IMW::StyleFloatVar frameStyle(ImGuiStyleVar_FrameRounding, buttonRounding < 0 ? .5f * actualButtonSize : buttonRounding);

            struct ButtonStyle {
                IMW::StyleColor normal, hover, active;

                ButtonStyle(ImVec4 _normal, ImVec4 _hover, ImVec4 _active) : normal(ImGuiCol_Button, std::move(_normal)), hover(ImGuiCol_ButtonHovered, std::move(_hover)), active(ImGuiCol_ButtonActive, std::move(_active)) {}
            };

            auto styles = [&]() -> std::optional<ButtonStyle> {
                if (!isTransparent) {
                    ImVec4 buttonColorHover  = detail::lightenColor(buttonColor, 0.5f);
                    ImVec4 buttonColorActive = detail::darkenColor(buttonColor, 0.7f);
                    buttonColor.w            = 1.0;
                    buttonColorHover.w       = 1.0;
                    buttonColorActive.w      = 1.0;

                    return std::make_optional<ButtonStyle>(buttonColor, buttonColorHover, buttonColorActive);
                } else {
                    return std::nullopt;
                }
            }();

            if (ImGui::Button(label.c_str(), ImVec2{actualButtonSize, actualButtonSize})) {
                isClicked = true;
            }
        }

        if (ImGui::IsItemHovered() && !toolTip.empty()) {
            ImGui::SetTooltip("%s", toolTip.c_str());
        }

        return isClicked;
    }
};

enum class MenuType { Radial, Vertical, Horizontal };

template<std::size_t unique_id, MenuType menuType>
class PopupMenu {
    static std::vector<MenuButton> _buttons;
    static const std::string       _popupId;
    static ImRect                  _itemBoundaryBox;
    static float                   _animationProgress;
    static bool                    _isOpen;
    const float                    _padding = ImGui::GetStyle().WindowPadding.x;
    ImVec2                         _menuSize;
    float                          _startAngle       = 0.f;
    float                          _stopAngle        = 90.f;
    float                          _extraRadius      = 0.f;
    float                          _animationSpeed   = 0.25f;
    float                          _timeOut          = 0.5f; // time in seconds to close the menu when the mouse is out of range
    float                          _autoCloseTimeOut = 5.0f; // time in seconds to close the menu when the mouse is out of range

    //
    [[nodiscard]] float maxButtonSize(std::size_t firstButtonIndex = 0) const {
        if (_buttons.empty()) {
            return std::max(_menuSize.x, _menuSize.y);
        }
        float max = 0.0;
        for (std::size_t index = firstButtonIndex; index < _buttons.size(); ++index) {
            max = std::max(max, _buttons[index].size());
        }
        return max;
    }

    [[nodiscard]] std::pair<std::size_t, float> maxButtonNumberAndSizeForArc(float baseArcRadius, std::size_t firstButtonIndex = 0) const {
        const float totalAngle       = _stopAngle - _startAngle;
        const float arcLength        = std::numbers::pi_v<float> * baseArcRadius * (totalAngle / 180.f);
        std::size_t buttonCount      = 0UL;
        float       maxButtonSize    = .0f;
        float       cumulativeLength = .0f;
        for (auto i = firstButtonIndex; i < _buttons.size(); ++i) {
            const auto& button     = _buttons[i];
            const float buttonSize = button.size();
            if ((cumulativeLength + buttonSize + _padding) > arcLength || (button.isNewRow && i > firstButtonIndex)) {
                break;
            }
            ++buttonCount;
            maxButtonSize = std::max(buttonSize, maxButtonSize);
            cumulativeLength += buttonSize + _padding;
        }
        return {buttonCount, maxButtonSize};
    }

    void updateElementCoordinate() {
        _itemBoundaryBox.Min = ImGui::GetItemRectMin();
        _itemBoundaryBox.Max = ImGui::GetItemRectMax();
        _menuSize            = _itemBoundaryBox.GetSize();
    }

public:
    float frameRounding = 6.f;

    explicit PopupMenu(ImVec2 menuSize = {100, 100}, float startAngle = 0.f, float stopAngle = 360.f, float extraRadius = 0.f, float animationSpeed = .25f, float timeOut = 0.5f) : _menuSize(menuSize), _startAngle(startAngle), _stopAngle(stopAngle), _extraRadius(extraRadius), _animationSpeed(animationSpeed), _timeOut(timeOut) { updateAndDraw(); }

    template<bool transparent = false, bool newRow = false>
    void addButton(std::string label, auto onClick, float buttonSize = -1.f, std::string toolTip = "") {
        if (_animationProgress > 0.f) { // we do not allow to add buttons when the popup is already open or animating
            return;
        }
        updateElementCoordinate();
        _buttons.emplace_back(label, "", buttonSize, std::move(onClick), nullptr, std::move(toolTip), transparent, newRow);
        _isOpen = true;
    }

    template<bool transparent = false, bool newRow = false>
    void addButton(std::string label, auto onClick, ImFont* font, std::string toolTip = "") {
        if (_animationProgress > 0.f) { // we do not allow to add buttons when the popup is already open or animating
            return;
        }
        updateElementCoordinate();
        float buttonSize = 0.f;
        if (font == nullptr) {
            buttonSize = ImGui::CalcTextSize(label.c_str()).y + 2.f * _padding;
        } else {
            buttonSize = font->FontSize + 2.f * _padding;
        }
        _buttons.emplace_back(label, "", buttonSize, std::move(onClick), font, std::move(toolTip), transparent, newRow);
        _isOpen = true;
    }

    [[nodiscard]] bool isOpen() const noexcept { return _isOpen; }

    void forceClose() {
        _isOpen            = false;
        _animationProgress = 0.0;
        _buttons.clear();
        ImGui::CloseCurrentPopup();
    }

    void updateAndDraw() {
        static float timeOutOfRadius = 0.0f;

        const float deltaTime = ImGui::GetIO().DeltaTime;
        _animationProgress    = _isOpen ? std::min(1.0f, _animationProgress + deltaTime / _animationSpeed) : std::max(0.0f, _animationProgress - deltaTime / _animationSpeed);

        if (!_isOpen && _animationProgress <= 0.f) {
            _itemBoundaryBox = {{-1.f, -1.f}, {-1.f, -1.f}};
            if (!_buttons.empty()) {
                _buttons.clear();
            }
            return;
        } else if (_isOpen && (_itemBoundaryBox.Min.x <= 0.f || _itemBoundaryBox.Min.y <= 0.f || _itemBoundaryBox.Max.x <= 0.f || _itemBoundaryBox.Max.y <= 0.f)) {
            _itemBoundaryBox = {ImGui::GetMousePos(), ImGui::GetMousePos()};
        }
        const ImVec2 centre = _itemBoundaryBox.GetCenter();

        std::size_t nButtonRows = 1UL;
        const float buttonSize  = maxButtonSize();
        if (_animationProgress >= 0.0f) {
            const ImVec2 oldPos            = ImGui::GetCursorPos();
            float        requiredPopupSize = 2.f * (_extraRadius + (buttonSize + 2.f * _padding) * static_cast<float>(_buttons.size()));
            ImGui::SetNextWindowSize(ImVec2(requiredPopupSize, requiredPopupSize));
            ImGui::SetNextWindowPos(ImVec2(centre.x - .5f * requiredPopupSize / 2, centre.y - .5f * requiredPopupSize));

            ImGui::OpenPopup(_popupId.c_str());
            if (auto popup = IMW::Popup(_popupId.c_str(), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration)) {
                if constexpr (menuType == MenuType::Radial) {
                    nButtonRows = drawButtonsOnArc(centre, _extraRadius + .5f * buttonSize + _padding);
                } else {
                    nButtonRows = drawButtonsVertically();
                }
            }
            ImGui::SetCursorScreenPos(oldPos);
        }

        const ImVec2 mousePos      = ImGui::GetMousePos();
        const float  mouseDistance = std::hypot(mousePos.x - centre.x, mousePos.y - centre.y);
        const float  mouseAngle    = std::atan2(mousePos.y - centre.y, mousePos.x - centre.x) * 180.0f / std::numbers::pi_v<float>;
        const float  arcRadius     = _extraRadius + static_cast<float>(nButtonRows + 1) * (buttonSize + _padding);
        // the last statement is to keep the menu open when the mouse is outside the arc segment but on the calling button.
        const bool mouseInArc = (mouseDistance <= arcRadius && mouseAngle >= _startAngle && mouseAngle <= _stopAngle) || mouseDistance <= std::max(_menuSize.x, _menuSize.y);
        timeOutOfRadius       = !mouseInArc ? timeOutOfRadius + deltaTime : 0.f;
        if (timeOutOfRadius >= _timeOut) {
            _isOpen = false;
        }

        if (timeOutOfRadius >= _timeOut) {
            _isOpen = false;
        }

#ifdef __EMSCRIPTEN__

#else
        if ((mouseInactivity() > _autoCloseTimeOut) && _isOpen && mouseDistance > std::max(_menuSize.x, _menuSize.y)) {
            _isOpen = false;
        }
#endif
    }

private:
    void drawButton(MenuButton& button) {
        if (button.create(menuType == MenuType::Radial ? -1.f : frameRounding)) {
            std::visit(
                [&]<typename Arg>(Arg&& onClick) {
                    using T = std::decay_t<decltype(onClick)>;
                    if constexpr (std::is_same_v<T, std::function<void()>>) {
                        onClick();
                    } else if constexpr (std::is_same_v<T, std::function<void(MenuButton&)>>) {
                        onClick(button);
                    }
                },
                button.onClick);
        }
    }

    [[nodiscard]] std::size_t drawButtonsOnArc(const ImVec2& centre, float arcRadius) {
        std::size_t currentRow = 0UL;
        for (std::size_t buttonIndex = 0UL; buttonIndex < _buttons.size();) {
            // get the maximum number of buttons and button size for the current arc row
            const auto [maxButtonsInRow, maxButtonSizeInRow] = maxButtonNumberAndSizeForArc(arcRadius, buttonIndex);

            float cumulativeAngle = _startAngle;
            for (std::size_t buttonsInRow = 0UL; buttonIndex < _buttons.size() && buttonsInRow < maxButtonsInRow; ++buttonIndex) {
                auto&       button     = _buttons[buttonIndex];
                const float buttonSize = button.size();
                // centre button if there is only one in the arc segment
                const float angle = cumulativeAngle + ((maxButtonsInRow == 1) ? 0.5f * (_stopAngle - _startAngle) : 0.5f * ((buttonSize + _padding) / arcRadius) * (180.0f / std::numbers::pi_v<float>));

                const float angleRad = angle * _animationProgress * (std::numbers::pi_v<float> / 180.0f);
                ImGui::SetCursorScreenPos(ImVec2(centre.x + arcRadius * std::cos(angleRad) - 0.5f * buttonSize, centre.y + arcRadius * std::sin(angleRad) - 0.5f * buttonSize));
                drawButton(button);
                cumulativeAngle += ((buttonSize + _padding) / arcRadius) * (180.0f / std::numbers::pi_v<float>); // Update the cumulative angle based on the button size
                ++buttonsInRow;
            }
            ++currentRow;
            arcRadius += 0.5f * maxButtonSizeInRow + 0.5f * _padding + 0.5f * maxButtonNumberAndSizeForArc(arcRadius, buttonIndex).second; // update arc radius
        }
        return currentRow;
    }

    [[nodiscard]] std::size_t drawButtonsVertically() {
        if (_buttons.empty()) {
            return 0;
        }

        const float maxSize = maxButtonSize();
        ImVec2      posBeneathMenu{_itemBoundaryBox.Min.x, _itemBoundaryBox.Max.y + _padding};

        std::size_t nRows = 0UL;
        for (MenuButton& button : _buttons) {
            const float buttonSize = button.size();
            posBeneathMenu.x       = _itemBoundaryBox.Min.x + .5f * maxSize - .5f * buttonSize;
            ImGui::SetCursorScreenPos(posBeneathMenu);
            drawButton(button);
            ++nRows;

            posBeneathMenu.y += buttonSize + button.padding;
        }
        return nRows;
    }

    float mouseInactivity() {
        static float timeSinceLastIoActivity = 0.0;
        if (const ImGuiIO& io = ImGui::GetIO(); !_isOpen || io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)) {
            timeSinceLastIoActivity = 0.f;
        } else {
            timeSinceLastIoActivity += io.DeltaTime;
        }
        return timeSinceLastIoActivity;
    }
};
template<std::size_t unique_id, MenuType menuType>
bool PopupMenu<unique_id, menuType>::_isOpen = false;
template<std::size_t unique_id, MenuType menuType>
const std::string PopupMenu<unique_id, menuType>::_popupId = fmt::format("MenuPopup_{}", unique_id);
template<std::size_t unique_id, MenuType menuType>
ImRect PopupMenu<unique_id, menuType>::_itemBoundaryBox = {{-1.f, -1.f}, {-1.f, -1.f}};
template<std::size_t unique_id, MenuType menuType>
float PopupMenu<unique_id, menuType>::_animationProgress = 0.f;
template<std::size_t unique_id, MenuType menuType>
std::vector<MenuButton> PopupMenu<unique_id, menuType>::_buttons;

template<std::size_t unique_id>
using RadialCircularMenu = PopupMenu<unique_id, MenuType::Radial>;

template<std::size_t unique_id>
using VerticalPopupMenu = PopupMenu<unique_id, MenuType::Vertical>;

} // namespace DigitizerUi

#endif // OPENDIGITIZER_POPUPMENU_HPP
