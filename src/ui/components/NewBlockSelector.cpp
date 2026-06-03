#include "NewBlockSelector.hpp"
#include "InputTextCompletion.hpp"

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

#include "../components/Dialog.hpp"
#include "../components/ListBox.hpp"

#include "../ui/GraphModel.hpp"

#include "NewBlockSelectorFuzzySearch.hpp"
#include "scope_exit.hpp"

#include <gnuradio-4.0/Message.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

using namespace std::string_literals;
using DigitizerUi::components::FilterResult;
using DigitizerUi::components::filterTypename;
using DigitizerUi::components::NamespaceDelimitedSearchIterator;

struct Symbol {
    std::string_view    fullSymbolName;
    const FilterResult& filterResult;

    /// This draws the full name of the symbol with exact search matches highlighted. It is also a selectable and returns true if it was selected.
    [[nodiscard]] bool draw(std::string_view currentlySelected, bool scrollToSelected) const {
        bool selected = false;
        assert(!fullSymbolName.empty());
        auto       buttonWidth       = ImGui::GetContentRegionAvail().x;
        const auto textCursorInitial = ImGui::GetCursorScreenPos() + ImVec2{ImGui::GetStyle().ItemInnerSpacing.x, 0.f};

        const auto* window = ImGui::GetCurrentWindow();

        const bool wasSelected = fullSymbolName == currentlySelected;
        if (ImGui::Selectable(std::format("##{}", fullSymbolName).c_str(), wasSelected, ImGuiSelectableFlags_None, {buttonWidth, 0.f})) {
            selected = true;
        }

        if (wasSelected && scrollToSelected) {
            DigitizerUi::components::detail::ensureItemVisible();
        }

        const auto fontSize = ImGui::GetFontSize();

        const auto& matches = filterResult.exactMatches;

        for (std::string_view exactMatch : matches) {
            assert(exactMatch.data() >= fullSymbolName.data() && exactMatch.data() + exactMatch.size() <= fullSymbolName.data() + fullSymbolName.size());

            const char* matchStart = exactMatch.data();
            const char* matchEnd   = exactMatch.data() + exactMatch.size();
            const auto  boxPos     = textCursorInitial + ImVec2{ImGui::CalcTextSize(fullSymbolName.data(), matchStart).x, 0.0};
            const auto  boxSize    = ImGui::CalcTextSize(matchStart, matchEnd);
            window->DrawList->AddRectFilled(boxPos, boxPos + boxSize, ImGui::ColorConvertFloat4ToU32(DigitizerUi::LookAndFeel::instance().palette().highlightedSearchResultsBg));
        }

        ImFont*    font      = DigitizerUi::LookAndFeel::instance().fontNormal[DigitizerUi::LookAndFeel::instance().prototypeMode];
        const auto textColor = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]);
        window->DrawList->AddText(font, fontSize, textCursorInitial, textColor, fullSymbolName.data(), fullSymbolName.data() + fullSymbolName.size());
        return selected;
    }
};

/// A node in a tree of namespaces (branches) + symbols (leaf nodes). Built each frame based on the full typenames of registered blocks
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

    NamespaceOrSymbol() = delete;
    explicit NamespaceOrSymbol(const Symbol& symbol) : variant(symbol) {}
    explicit NamespaceOrSymbol(NamespaceTag) : variant(Namespace{}) {}

    NamespaceOrSymbol* insertOrFindNameSpaceChild(std::string_view key) { //
        return nameSpace()->insertOrLeaveExistingElement(key, NamespaceTag{});
    }
    void insertElementChild(std::string_view key, std::string_view element, const FilterResult& filter) { //
        nameSpace()->insertOrLeaveExistingElement(key, Symbol{element, filter});
    }

    /// If nonempty, the returned string view is the full symbol name of the selected symbol
    [[nodiscard]] std::string_view drawTreeRecursive(std::string_view currentlySelected, std::string& keyBuffer, bool scrollToSelected, std::optional<bool> newOpenValue) const {
        std::string_view selected;
        assert(nameSpace());

        for (const auto& [key, child] : nameSpace()->children) {
            const auto namespaceCase = [&keyBuffer, key, &newOpenValue, &child, &selected, scrollToSelected, currentlySelected](const Namespace&) {
                // this is a branch
                keyBuffer = key;
                DigitizerUi::IMW::ChangeStrId id(keyBuffer.c_str());
                if (newOpenValue) {
                    ImGui::SetNextItemOpen(*newOpenValue);
                }
                if (ImGui::TreeNodeEx(keyBuffer.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DrawLinesFull)) {
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

private:
    Namespace*                      nameSpace() { return std::get_if<Namespace>(&variant); }
    const Namespace*                nameSpace() const { return std::get_if<Namespace>(&variant); }
    std::variant<Symbol, Namespace> variant;
};

namespace DigitizerUi {
void NewBlockSelector::drawNamespaceTree(const ImVec2& size) {
    IMW::Child window("##blockSelector", size, ImGuiChildFlags_None, ImGuiWindowFlags_None);

    constexpr const char* clearButtonText  = "Clear";
    constexpr const char* foldButtonText   = "Fold all";
    constexpr const char* unfoldButtonText = "Unfold all";
    const auto            originalCursorX  = ImGui::GetCursorPosX();
    const auto            buttonStart      = ImGui::GetContentRegionAvail().x - (IMW::CalcAdjacentButtonSizes(std::array{clearButtonText, foldButtonText, unfoldButtonText})).x;
    bool                  scrollToSelected = false;
    ImGui::SetCursorPosX(buttonStart);
    if (ImGui::Button(clearButtonText)) {
        m_blockFilter.clear();
        scrollToSelected = true; // more things are visible now that there is no filter
    }
    std::optional<bool> newUnfoldedValue;
    ImGui::SameLine();
    if (ImGui::Button(foldButtonText)) {
        newUnfoldedValue = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(unfoldButtonText)) {
        newUnfoldedValue = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(originalCursorX);

    ImGui::SetNextItemWidth(buttonStart - ImGui::GetStyle().ItemSpacing.x); // since the InputTextCompletion has no label, this makes sense, otherwise subtract label width + spacing

    if (auto completionNamesView = data | std::views::keys; DigitizerUi::components::InputTextCompletion(completionNamesView).inputText("##filterTypenameType", &m_blockFilter)) {
        newUnfoldedValue = true; // user typed in the input filter, they probably want to see all matches
        scrollToSelected = true;
    }
    if (!m_wasOpenLastFrame) {
        ImGui::SetKeyboardFocusHere(-1);
    }

    struct FilteredEntry {
        decltype(data)::iterator      originalElement;
        std::unique_ptr<FilterResult> filterResult;

        [[nodiscard]] constexpr std::string_view blockTypeName() const { return originalElement->first; }
    };

    // create filter results for every entry in `data`, sorted by relevance. results are
    // passed when drawing the tree, so as to highlight matching substrings
    const std::vector<FilteredEntry> filterResults = [this] {
        std::vector<FilteredEntry> result;
        result.reserve(this->data.size());

        for (auto iterator = std::begin(this->data); iterator != std::end(this->data); ++iterator) {
            std::string_view blockType{iterator->first};
            result.emplace_back(iterator, std::make_unique<FilterResult>(filterTypename(blockType, m_blockFilter)));
        }

        if (this->m_blockFilter.empty()) {
            return result;
        }

        // sort by relevance to the filter query
        std::ranges::stable_sort(result, std::ranges::greater(), [](const FilteredEntry& entry) { return entry.filterResult->score; });

        // remove all but roughly what will fit on the screen
        const auto maxEntries = static_cast<std::size_t>(std::ceilf((ImGui::GetContentRegionAvail().y / ImGui::GetTextLineHeightWithSpacing()) / 2.f));
        if (result.size() > maxEntries) {
            result.resize(maxEntries);
        }

        return result;
    }();

    // if selected element is no longer available, deselect it
    if (std::ranges::none_of(filterResults, [this](const FilteredEntry& entry) { return entry.blockTypeName() == m_currentlySelectedType; })) {
        m_currentlySelectedType.clear();
    }

    auto globalNamespace = std::make_unique<NamespaceOrSymbol>(NamespaceOrSymbol::NamespaceTag{});
    for (const FilteredEntry& entry : filterResults) {
        NamespaceOrSymbol* cursor = globalNamespace.get();
        using namespace std::literals;
        for (const auto& namespaceElement : entry.blockTypeName() | std::views::split("::"sv)) {
            if (namespaceElement.end() == entry.blockTypeName().end()) { // last element in thing::thing::thing chain
                cursor->insertElementChild(std::string_view{namespaceElement}, entry.blockTypeName(), *entry.filterResult);
            } else {
                cursor = cursor->insertOrFindNameSpaceChild(std::string_view{namespaceElement});
            }
        }
    }

    std::string tempKeyBuffer;
    auto        newSelected = globalNamespace->drawTreeRecursive(this->m_currentlySelectedType, tempKeyBuffer, scrollToSelected, newUnfoldedValue);
    if (!newSelected.empty()) {
        this->m_currentlySelectedType = newSelected;
    }
}

void NewBlockSelector::draw() {
    const auto windowSize = ImGui::GetIO().DisplaySize - ImVec2(32, 32);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Once);
    auto x = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(x + 32);

    auto menu = IMW::ModalPopup(m_windowName.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!menu) {
        m_wasOpenLastFrame = false;
        return;
    }
    Digitizer::utils::scope_exit _([this] { this->m_wasOpenLastFrame = true; });

    auto listSize = windowSize - ImVec2(64, 64);
    listSize.x /= 2.f;

    this->drawNamespaceTree(listSize);

    // If the type is empty, we treat it as invalid,
    // but some types might not have parametrizations,
    // that is their empty parametrization is valid,
    // so we need std::optional
    std::optional<std::string> selectedParametrization;

    if (!m_currentlySelectedType.empty()) {
        if (auto typeIt = data.find(m_currentlySelectedType); typeIt != data.cend()) {
            ImGui::SameLine();

            if (m_currentlySelectedType != m_previouslySelectedType) {
                m_previouslySelectedType              = m_currentlySelectedType;
                m_selectedTypeParametrizationListName = "parametrizations_for_" + m_currentlySelectedType;
            }

            auto parameterization = components::FilteredListBox(m_selectedTypeParametrizationListName.c_str(), listSize, //
                typeIt->second, [index = 0](auto& parametrization) mutable -> std::pair<int, std::string> {
                    index++;
                    return std::pair{index, parametrization};
                });

            if (parameterization) {
                selectedParametrization = parameterization.value().second;
            }
        }
    }

    const bool okEnabled = !m_currentlySelectedType.empty();
    if (components::DialogButtons(okEnabled) == components::DialogButton::Ok) {
        if (!m_currentlySelectedType.empty() && selectedParametrization) {
            gr::Message message;
            std::string type    = std::format("{}{}", m_currentlySelectedType, selectedParametrization.value_or(std::string{}));
            message.cmd         = gr::message::Command::Set;
            message.endpoint    = gr::scheduler::property::kEmplaceBlock;
            message.serviceName = m_targetSchedulerUniqueName;
            message.data        = gr::property_map{{"type", std::move(type)}};
            if (!m_targetGraphUniqueName.empty()) {
                (*message.data)["_targetGraph"] = m_targetGraphUniqueName;
            }
            m_graphModel->sendMessage(message);
        }
    }
}
} // namespace DigitizerUi
