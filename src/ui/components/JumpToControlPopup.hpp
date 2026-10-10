#ifndef OPENDIGITIZER_UI_COMPONENTS_JUMP_TO_CONTROL_POPUP_HPP
#define OPENDIGITIZER_UI_COMPONENTS_JUMP_TO_CONTROL_POPUP_HPP

#include <functional>
#include <string>

namespace DigitizerUi {
class UiGraphModel;
struct UiGraphBlock;
} // namespace DigitizerUi

namespace DigitizerUi::components {

struct JumpToControlPopup {
    std::string blockUniqueName;
    std::string property;
    bool        opened = false;

    /// returns false if the popup does not name an existing block, or it has been closed
    bool draw(UiGraphModel* graphModel, std::function<void(const std::string&)> focusBlockCallback);
};

} // namespace DigitizerUi::components

#endif
