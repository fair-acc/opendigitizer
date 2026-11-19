#ifndef OPENDIGITIZER_UI_COMPONENTS_NEW_BLOCK_SELECTOR_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_NEW_BLOCK_SELECTOR_HPP_

#include <imgui.h>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace gr::od {

struct FilteredListState {
    std::optional<std::string> selected;
    std::string                filterString;
    bool                       filterInputReclaimFocus = false;
};

struct NewBlockSelector {
    std::map<std::string, std::set<std::string>> data;

    std::string _windowName = "New Block";

    bool        _wasOpenLastFrame = false;
    std::string _blockFilter;
    std::string _currentlySelectedType;
    std::string _previouslySelectedType;
    std::string _selectedTypeParametrizationListName;

    std::map<std::string, FilteredListState, std::less<>> _parametrizationLists;

    std::function<void(std::string)> _onTypeSelected;

    void drawNamespaceTree(const ImVec2& size);

    /// onTypeSelected is invoked with the chosen type when the user confirms with OK
    void open(std::function<void(std::string)> onTypeSelected) {
        _onTypeSelected = std::move(onTypeSelected);
        assert(_onTypeSelected);
        ImGui::OpenPopup(_windowName.c_str());
    }
    void draw();
};
} // namespace gr::od

#endif
