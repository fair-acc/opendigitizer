#include "NewBlockSelector.hpp"
#include "InputTextCompletion.hpp"

#include "../common/Imw.hpp"
#include "../common/LookAndFeel.hpp"

#include "../components/Dialog.hpp"
#include "../components/StyledWidgets.hpp"

#include "NewBlockSelectorFuzzySearch.hpp"
#include "stdex/tolower.hpp"

#include <gnuradio-4.0/meta/utils.hpp>

using namespace gr::od;

using namespace std::string_literals;
using components::FilterResult;
using components::filterTypename;

namespace gr::od {
namespace {
void ensureItemVisible() {
    auto scroll = ImGui::GetScrollY();
    auto min    = ImGui::GetWindowContentRegionMin().y + scroll;
    auto max    = ImGui::GetWindowContentRegionMax().y + scroll;

    auto h = ImGui::GetItemRectSize().y;
    auto y = ImGui::GetCursorPosY() - scroll;
    if (y > max) {
        ImGui::SetScrollHereY(1);
    } else if (y - h < min) {
        ImGui::SetScrollHereY(0);
    }
}

[[nodiscard]] std::optional<std::string> filteredListBox(const char* id, const ImVec2& size, std::vector<std::string>& names, FilteredListState& state) {
    imw::ChangeStrId newId(id);

    imw::Group group;

    auto x = ImGui::GetCursorPosX();
    auto y = ImGui::GetCursorPosY();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Filter:");
    ImGui::SameLine();
    ImGui::SetCursorPosY(y);

    if (ImGui::IsWindowAppearing() || state.filterInputReclaimFocus) {
        ImGui::SetKeyboardFocusHere();
        state.filterInputReclaimFocus = false;
    }

    imw::ItemWidth newItemWidth(size.x - (ImGui::GetCursorPosX() - x));
    const bool     shouldScrollToSelected = components::inputTextCompletion(names).inputText("##filterBlockType", &state.filterString);

    if (auto listbox = imw::ListBox("##Available Block types", ImVec2{size.x, size.y - (ImGui::GetCursorPosY() - y)})) {
        const auto matchesFilter = [&state](std::string_view name) { return !name.empty() && digitizer::utils::case_insensitive_contains(name, state.filterString); };
        if (state.selected && !matchesFilter(*state.selected)) {
            state.selected.reset();
        }

        int  selectOffset     = 0;
        bool scrollToSelected = shouldScrollToSelected;
        if (state.selected) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                selectOffset     = 1;
                scrollToSelected = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                selectOffset     = -1;
                scrollToSelected = true;
            }
        }

        const auto filtered = names | std::views::filter(matchesFilter) | std::ranges::to<std::vector>();
        for (auto it = filtered.begin(); it != filtered.end(); ++it) {
            if (!state.selected) {
                state.selected = *it;
            }

            if (const bool isLastIteration = (it + 1) == filtered.end(); !isLastIteration) {
                if (selectOffset == -1 && *(it + 1) == *state.selected) {
                    state.selected = *it;
                    selectOffset   = 0;
                } else if (selectOffset == 1 && *it == *state.selected) {
                    state.selected.reset();
                    selectOffset = 0;
                }
            }

            if (selectable(it->c_str(), state.selected && *it == *state.selected)) {
                state.selected                = *it;
                state.filterInputReclaimFocus = true;
            }
            if (state.selected && *state.selected == *it && scrollToSelected) {
                ensureItemVisible();
            }
        }
    }

    return state.selected;
}
} // namespace
} // namespace gr::od

struct Symbol {
    std::string_view    fullSymbolName;
    const FilterResult& filterResult;

    /// true when selected; exact search matches are highlighted
    [[nodiscard]] bool draw(std::string_view currentlySelected, bool scrollToSelected) const {
        bool selected = false;
        assert(!fullSymbolName.empty());
        auto       buttonWidth       = ImGui::GetContentRegionAvail().x;
        const auto textCursorInitial = ImGui::GetCursorScreenPos() + ImVec2{ImGui::GetStyle().ItemInnerSpacing.x, 0.f};

        const auto* window = ImGui::GetCurrentWindow();

        const bool wasSelected = fullSymbolName == currentlySelected;
        if (selectable(std::format("##{}", fullSymbolName).c_str(), wasSelected, ImGuiSelectableFlags_None, {buttonWidth, 0.f})) {
            selected = true;
        }

        if (wasSelected && scrollToSelected) {
            ensureItemVisible();
        }

        const auto fontSize = ImGui::GetFontSize();

        const auto& matches = filterResult.exactMatches;

        for (std::string_view exactMatch : matches) {
            assert(exactMatch.data() >= fullSymbolName.data() && exactMatch.data() + exactMatch.size() <= fullSymbolName.data() + fullSymbolName.size());

            const char* matchStart = exactMatch.data();
            const char* matchEnd   = exactMatch.data() + exactMatch.size();
            const auto  boxPos     = textCursorInitial + ImVec2{ImGui::CalcTextSize(fullSymbolName.data(), matchStart).x, 0.0};
            const auto  boxSize    = ImGui::CalcTextSize(matchStart, matchEnd);
            window->DrawList->AddRectFilled(boxPos, boxPos + boxSize, ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().highlightedSearchResultsBg), LookAndFeel::instance().searchHighlightRounding);
        }

        ImFont*    font      = LookAndFeel::instance().fontNormal();
        const auto textColor = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]);
        window->DrawList->AddText(font, fontSize, textCursorInitial, textColor, fullSymbolName.data(), fullSymbolName.data() + fullSymbolName.size());
        return selected;
    }
};

/// a node in a tree of namespaces (branches) and symbols (leaves), rebuilt each frame from the full type names of the registered blocks
struct NamespaceOrSymbol {
    struct NamespaceTag {};

    struct Namespace {
        using Pair = std::pair<std::string_view, std::unique_ptr<NamespaceOrSymbol>>;
        std::vector<Pair> children;

        template<typename... ConstructorArgs>
        NamespaceOrSymbol* insertOrLeaveExistingElement(std::string_view key, ConstructorArgs&&... args) {
            auto iter = std::ranges::find(children, key, &Pair::first);
            if (iter != std::end(children)) {
                return iter->second.get();
            }
            return children.emplace_back(key, std::make_unique<NamespaceOrSymbol>(std::forward<ConstructorArgs>(args)...)).second.get();
        }
    };

    std::variant<Symbol, Namespace> variant;

    NamespaceOrSymbol() = delete;
    explicit NamespaceOrSymbol(const Symbol& symbol) : variant(symbol) {}
    explicit NamespaceOrSymbol(NamespaceTag) : variant(Namespace{}) {}

    [[nodiscard]] NamespaceOrSymbol* insertOrFindNameSpaceChild(std::string_view key) { //
        auto* namespaceNode = nameSpace();
        if (namespaceNode == nullptr) {
            assert(false && "insertOrFindNameSpaceChild requires a namespace node");
            return nullptr;
        }
        return namespaceNode->insertOrLeaveExistingElement(key, NamespaceTag{});
    }
    void insertElementChild(std::string_view key, std::string_view element, const FilterResult& filter) { //
        auto* namespaceNode = nameSpace();
        if (namespaceNode == nullptr) {
            assert(false && "insertElementChild requires a namespace node");
            return;
        }
        namespaceNode->insertOrLeaveExistingElement(key, Symbol{element, filter});
    }

    /// if non-empty, the full symbol name of the selected symbol
    [[nodiscard]] std::string_view drawTreeRecursive(std::string_view currentlySelected, std::string& keyBuffer, bool scrollToSelected, std::optional<bool> newOpenValue) const {
        std::string_view selected;
        const auto*      namespaceNode = nameSpace();
        if (namespaceNode == nullptr) {
            assert(false && "drawTreeRecursive requires a namespace node");
            return {};
        }

        for (const auto& [key, child] : namespaceNode->children) {
            const auto namespaceCase = [&keyBuffer, key, &newOpenValue, &child, &selected, scrollToSelected, currentlySelected](const Namespace&) {
                keyBuffer = key;
                imw::ChangeStrId id(keyBuffer.c_str());
                if (newOpenValue) {
                    ImGui::SetNextItemOpen(*newOpenValue);
                }
                if (treeNodeEx(keyBuffer.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DrawLinesFull)) {
                    if (auto maybeSelected = child->drawTreeRecursive(currentlySelected, keyBuffer, scrollToSelected, newOpenValue); !maybeSelected.empty()) {
                        selected = maybeSelected;
                    }
                    ImGui::TreePop();
                }
            };
            const auto symbolCase = [currentlySelected, scrollToSelected, &selected](const Symbol& symbol) {
                if (symbol.draw(currentlySelected, scrollToSelected)) {
                    selected = symbol.fullSymbolName;
                }
            };
            std::visit(gr::meta::overloaded{namespaceCase, symbolCase}, child->variant);
        }

        return selected;
    }

    Namespace*       nameSpace() { return std::get_if<Namespace>(&variant); }
    const Namespace* nameSpace() const { return std::get_if<Namespace>(&variant); }
};

namespace gr::od {
void NewBlockSelector::drawNamespaceTree(const ImVec2& size) {
    imw::Child window("##blockSelector", size, ImGuiChildFlags_None, ImGuiWindowFlags_None);

    constexpr const char* kClearButtonText  = "Clear";
    constexpr const char* kFoldButtonText   = "Fold all";
    constexpr const char* kUnfoldButtonText = "Unfold all";
    const auto            originalCursorX   = ImGui::GetCursorPosX();
    const auto            buttonStart       = ImGui::GetContentRegionAvail().x - (imw::calcAdjacentButtonSizes(std::array{kClearButtonText, kFoldButtonText, kUnfoldButtonText}, calcTextButtonSize)).x;
    bool                  scrollToSelected  = false;
    ImGui::SetCursorPosX(buttonStart);
    if (textButton(kClearButtonText)) {
        _blockFilter.clear();
        scrollToSelected = true; // more things are visible now that there is no filter
    }
    std::optional<bool> newUnfoldedValue;
    ImGui::SameLine();
    if (textButton(kFoldButtonText)) {
        newUnfoldedValue = false;
    }
    ImGui::SameLine();
    if (textButton(kUnfoldButtonText)) {
        newUnfoldedValue = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(originalCursorX);

    ImGui::SetNextItemWidth(buttonStart - ImGui::GetStyle().ItemSpacing.x); // the input has no label

    if (auto completionNamesView = data | std::views::keys; components::inputTextCompletion(completionNamesView).inputText("##filterTypenameType", &_blockFilter)) {
        newUnfoldedValue = true; // user typed in the input filter, they probably want to see all matches
        scrollToSelected = true;
    }
    if (!_wasOpenLastFrame) {
        ImGui::SetKeyboardFocusHere(-1);
    }

    struct FilteredEntry {
        decltype(data)::iterator      originalElement;
        std::unique_ptr<FilterResult> filterResult;

        [[nodiscard]] constexpr std::string_view blockTypeName() const { return originalElement->first; }
    };

    // filter results for every entry in `data`, by relevance; the tree highlights their matching substrings
    const std::vector<FilteredEntry> filterResults = [this] {
        std::vector<FilteredEntry> result;
        result.reserve(this->data.size());

        for (auto iterator = std::begin(this->data); iterator != std::end(this->data); ++iterator) {
            std::string_view blockType{iterator->first};
            result.emplace_back(iterator, std::make_unique<FilterResult>(filterTypename(blockType, _blockFilter)));
        }

        if (this->_blockFilter.empty()) {
            return result;
        }

        std::ranges::stable_sort(result, std::ranges::greater(), [](const FilteredEntry& entry) { return entry.filterResult->score; });

        // remove all but roughly what will fit on the screen
        const auto maxEntries = static_cast<std::size_t>(std::ceilf((ImGui::GetContentRegionAvail().y / ImGui::GetTextLineHeightWithSpacing()) / 2.f));
        if (result.size() > maxEntries) {
            result.resize(maxEntries);
        }

        return result;
    }();

    if (std::ranges::none_of(filterResults, [this](const FilteredEntry& entry) { return entry.blockTypeName() == _currentlySelectedType; })) {
        _currentlySelectedType.clear();
    }

    auto globalNamespace = std::make_unique<NamespaceOrSymbol>(NamespaceOrSymbol::NamespaceTag{});
    for (const FilteredEntry& entry : filterResults) {
        NamespaceOrSymbol* cursor = globalNamespace.get();
        using namespace std::literals;
        for (const auto& namespaceElement : entry.blockTypeName() | std::views::split("::"sv)) {
            if (namespaceElement.end() == entry.blockTypeName().end()) {
                cursor->insertElementChild(std::string_view{namespaceElement}, entry.blockTypeName(), *entry.filterResult);
            } else {
                cursor = cursor->insertOrFindNameSpaceChild(std::string_view{namespaceElement});
                if (cursor == nullptr) {
                    break;
                }
            }
        }
    }

    std::string tempKeyBuffer;
    auto        newSelected = globalNamespace->drawTreeRecursive(this->_currentlySelectedType, tempKeyBuffer, scrollToSelected, newUnfoldedValue);
    if (!newSelected.empty()) {
        this->_currentlySelectedType = newSelected;
    }
}

void NewBlockSelector::draw() {
    const auto windowSize = ImGui::GetIO().DisplaySize - ImVec2(32, 32);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Once);
    auto x = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(x + 32);

    auto menu = Popup(_windowName.c_str(), PopupOptions{.flags = PopupFlags::WantsExclusiveFocus | PopupFlags::Modal | PopupFlags::WantsMaxSize, .desktopWindowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse});
    if (!menu) {
        _wasOpenLastFrame = false;
        return;
    }
    gr::on_scope_exit _([this] { this->_wasOpenLastFrame = true; });

    const ImVec2 listSize = [&windowSize] {
        if (LookAndFeel::instance().usesSheetPopups()) {
            constexpr float   kSeparatorThickness = 1.f; // of ImGui::Separator()
            const ImGuiStyle& style               = ImGui::GetStyle();
            const ImVec2      available           = ImGui::GetContentRegionAvail();
            // need to leave space for the dialog buttons at the bottom
            const float dialogButtonsHeight = 2.f * style.ItemSpacing.y + kSeparatorThickness + ImGui::GetFrameHeight();
            return ImVec2{(available.x - style.ItemSpacing.x) / 2.f, available.y - dialogButtonsHeight};
        }
        return ImVec2{(windowSize.x - 64.f) / 2.f, windowSize.y - 64.f};
    }();

    this->drawNamespaceTree(listSize);

    // nullopt: invalid; an empty parametrisation is valid for a type without parameters
    std::optional<std::string> selectedParametrization;

    if (!_currentlySelectedType.empty()) {
        if (auto typeIt = data.find(_currentlySelectedType); typeIt != data.cend()) {
            ImGui::SameLine();

            if (_currentlySelectedType != _previouslySelectedType) {
                _previouslySelectedType              = _currentlySelectedType;
                _selectedTypeParametrizationListName = "parametrizations_for_" + _currentlySelectedType;
            }

            auto names              = typeIt->second | std::views::transform([](const std::string& parametrization) { return parametrization.empty() ? "default"s : parametrization; }) | std::ranges::to<std::vector>();
            selectedParametrization = filteredListBox(_selectedTypeParametrizationListName.c_str(), listSize, names, _parametrizationLists[_selectedTypeParametrizationListName]);
        }
    }

    const bool okEnabled    = !_currentlySelectedType.empty();
    const auto dialogResult = components::dialogButtons(okEnabled);
    if (dialogResult == components::DialogButton::Ok && !_currentlySelectedType.empty() && selectedParametrization && _onTypeSelected) {
        std::string type = _currentlySelectedType;
        if (*selectedParametrization != "default"s) {
            type += *selectedParametrization;
        }
        _onTypeSelected(std::move(type));
    }
    if (dialogResult != components::DialogButton::None) {
        _onTypeSelected = {}; // release captured state once the popup closes
    }
}
} // namespace gr::od
