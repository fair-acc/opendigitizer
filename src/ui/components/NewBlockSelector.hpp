#ifndef OPENDIGITIZER_UI_COMPONENTS_NEW_BLOCK_SELECTOR_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_NEW_BLOCK_SELECTOR_HPP_

#include <set>
#include <string>

#include <gnuradio-4.0/Message.hpp>

#include "../common/ImguiWrap.hpp"

namespace DigitizerUi {

class UiGraphModel;

class NewBlockSelector {
private:
    std::string m_windowName = "New Block";

    std::string m_previouslySelectedType;
    std::string m_selectedTypeParametrizationListName;

    UiGraphModel* m_graphModel = nullptr;

public:
    void open() { ImGui::OpenPopup(m_windowName.c_str()); }
    void draw(const std::map<std::string, std::set<std::string>>& knownBlockTypes);

    void setGraphModel(UiGraphModel* newModel) { m_graphModel = newModel; }
};
} // namespace DigitizerUi

#endif
