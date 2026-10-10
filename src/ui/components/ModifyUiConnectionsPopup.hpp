#ifndef OPENDIGITIZER_UI_COMPONENTS_MODIFY_UI_CONNECTIONS_POPUP_HPP
#define OPENDIGITIZER_UI_COMPONENTS_MODIFY_UI_CONNECTIONS_POPUP_HPP

#include <string>
#include <vector>

namespace DigitizerUi {
class UiGraphModel;
class UiGraphBlock;
} // namespace DigitizerUi

namespace DigitizerUi::components {
// describes an incoming connection to a property, from a UI control block
struct UiControlConnection {
    std::string controlUniqueName;
    std::string controlName;
    bool        allBlocks = false;
    // checkbox state, used, for example, by the "Modify Connections" dialog
    bool        keep      = true; 
};

struct ModifyUiConnectionsPopup {
    std::string                      blockUniqueName;
    std::string                      property;
    std::vector<UiControlConnection> connections;
    bool                             opened = false;

    /// returns false if the popup does not name an existing block, or it has been closed
    bool draw(UiGraphModel* graphModel);
};

} // namespace DigitizerUi::components

#endif
