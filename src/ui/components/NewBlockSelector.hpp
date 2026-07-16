#ifndef OPENDIGITIZER_UI_COMPONENTS_NEW_BLOCK_SELECTOR_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_NEW_BLOCK_SELECTOR_HPP_

#include <imgui.h>

#include <functional>
#include <map>
#include <set>
#include <string>

namespace DigitizerUi {

class NewBlockSelector {
private:
    std::string m_windowName = "New Block";

    bool        m_wasOpenLastFrame = false;
    std::string m_blockFilter;
    std::string m_currentlySelectedType;
    std::string m_previouslySelectedType;
    std::string m_selectedTypeParametrizationListName;

    std::function<void(std::string)> m_onTypeSelected;

    void drawNamespaceTree(const ImVec2& size);

public:
    std::map<std::string, std::set<std::string>> data;

    /// onTypeSelected is invoked with the chosen type when the user confirms with OK
    void open(std::function<void(std::string)> onTypeSelected) {
        m_onTypeSelected = std::move(onTypeSelected);
        assert(m_onTypeSelected);
        ImGui::OpenPopup(m_windowName.c_str());
    }
    void draw();
};
} // namespace DigitizerUi

#endif
