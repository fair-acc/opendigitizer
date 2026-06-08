#include "NewBlockSelector.hpp"
#include "InputTextCompletion.hpp"

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

#include "../components/Dialog.hpp"
#include "../components/ListBox.hpp"

#include "../ui/GraphModel.hpp"

#include "tolower.hpp"

#include <gnuradio-4.0/Message.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

using namespace std::string_literals;

namespace {
/// Levenshtein distance
std::size_t wordDistance(std::string_view a, std::string_view b) {
    // easy case where the distance to empty string is always length
    if (a.size() == 0) {
        return b.size();
    }
    if (b.size() == 0) {
        return a.size();
    }

    std::vector<std::vector<std::size_t>> distancesMatrix(a.size() + 1, std::vector<std::size_t>(b.size() + 1));

    for (std::size_t index = 0; index <= a.size(); index++) {
        distancesMatrix[index][0] = index;
    }
    for (std::size_t index = 0; index <= b.size(); index++) {
        distancesMatrix[0][index] = index;
    }
    for (std::size_t y = 1; y <= a.size(); ++y) {
        for (std::size_t x = 1; x <= b.size(); ++x) {
            const std::size_t cost     = (b[x - 1] == a[y - 1]) ? 0 : 1;
            const auto        above    = distancesMatrix[y - 1][x] + 1;
            const auto        left     = distancesMatrix[y][x - 1] + 1;
            const auto        diagonal = distancesMatrix[y - 1][x - 1];
            distancesMatrix[y][x]      = std::min(std::min(above, left), diagonal + cost);
        }
    }
    return distancesMatrix[a.size()][b.size()];
}

struct FilterResult {
    std::vector<std::string_view> exactMatches; // not entirely exact, case insensitive
    std::size_t                   distance = std::numeric_limits<std::size_t>::max();
    double                        score    = 0.0;

    void addExactMatchesInString(std::string_view string, std::string_view filter) {
        std::string_view substring = string;
        while (!substring.empty()) {
            constexpr auto tolower = [](char c) { return Digitizer::utils::safe_tolower(c); };
            if (auto match = std::ranges::search(substring, filter, std::ranges::equal_to{}, tolower, tolower); !match.empty()) {
                assert(match.data() >= substring.data() && match.data() < substring.data() + substring.size());

                exactMatches.emplace_back(match);
                const auto matchStart = match.data() - substring.data();
                assert(matchStart >= 0);
                substring = substring.substr(static_cast<std::size_t>(matchStart) + match.size());
            } else {
                break;
            }
        }
    }
};

/// Find all splits of a string not containing ':' or ' ', in order to iterate
/// through a search which might be separate words (space delimiter) or in
/// namespace::format (colon delimiter)
/// It is sort of like std::views::split("::", ' ')
struct NamespaceDelimitedSearchIterator {
    std::string_view contents;

    constexpr std::optional<std::string_view> next() {
        // always remove delimiters + perform validation at the start, as the caller may have instantiated with an invalid contents
        while (!contents.empty() && (contents.front() == ':' || contents.front() == ' ')) {
            contents = contents.substr(1);
        }

        std::optional<std::string_view> result;
        if (!contents.empty()) {
            const std::size_t maybeNextIndex = std::min(contents.find(':'), contents.find(' '));
            if (maybeNextIndex >= contents.size()) {
                return std::exchange(contents, {});
            }

            result = contents.substr(0, maybeNextIndex);
            assert(!result->empty());
            contents = contents.substr(maybeNextIndex); // consume delimiters .next() time
        }
        return result;
    }
};

std::unique_ptr<FilterResult> filterBlock(std::string_view block, std::string_view filter) {
    auto result = std::make_unique<FilterResult>();
    if (filter.empty()) {
        return result;
    }

    std::size_t                      filterSizeWithoutDelimiters = 0;
    NamespaceDelimitedSearchIterator blockIterator{block};
    while (auto blockSubString = blockIterator.next()) {
        filterSizeWithoutDelimiters = 0;

        NamespaceDelimitedSearchIterator filterIterator{filter};
        while (auto filterSubString = filterIterator.next()) {
            result->addExactMatchesInString(*blockSubString, *filterSubString);

            const auto distance = wordDistance(*blockSubString, *filterSubString);

            // there is always some distance due to size difference, ignore that
            const auto guaranteedDistance = static_cast<std::size_t>(std::abs(static_cast<std::int64_t>(blockSubString->size()) - static_cast<std::int64_t>(filterSubString->size())));
            assert(distance >= guaranteedDistance);
            result->distance += distance - guaranteedDistance;

            filterSizeWithoutDelimiters += filterSubString->size();
        }
    }

    // final score heavily weights any exact matches
    result->score = (1.0 - static_cast<double>(result->distance) / static_cast<double>(filterSizeWithoutDelimiters)) + static_cast<double>(result->exactMatches.size());

    return result;
}

struct Symbol {
    std::string_view    fullSymbolName;
    const FilterResult& filterResult;

    /// This draws the full name of the symbol with exact search matches highlighted. It is also a selectable and returns true if it was selected.
    [[nodiscard]] bool draw(std::string_view currentlySelected) {
        bool selected = false;
        assert(!fullSymbolName.empty());
        auto       buttonWidth       = ImGui::GetContentRegionAvail().x;
        const auto textCursorInitial = ImGui::GetCursorScreenPos() + ImVec2{ImGui::GetStyle().ItemInnerSpacing.x, 0.f};

        const auto* window = ImGui::GetCurrentWindow();

        if (ImGui::Selectable(std::format("##{}", fullSymbolName).c_str(), fullSymbolName == currentlySelected, ImGuiSelectableFlags_None, {buttonWidth, 0.f})) {
            selected = true;
        }
        const auto fontSize = ImGui::GetFontSize();

        const auto& matches = filterResult.exactMatches;

        auto        textCursor  = textCursorInitial;
        const char* beforeMatch = fullSymbolName.data();
        for (std::string_view exactMatch : matches) {
            assert(exactMatch.data() >= fullSymbolName.data() && exactMatch.data() < fullSymbolName.data() + fullSymbolName.size());
            textCursor.x += ImGui::CalcTextSize(beforeMatch, exactMatch.data()).x;
            beforeMatch = exactMatch.data() + exactMatch.size();
            // draw boxes under the matched text
            auto highlightedSize = ImGui::CalcTextSize(exactMatch.data(), beforeMatch);
            window->DrawList->AddRectFilled(textCursor, textCursor + highlightedSize, rgbToImGuiABGR(0xFFFF00, 0x88));
            textCursor.x += highlightedSize.x;
        }

        auto textColor = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]);

        ImFont* font = DigitizerUi::LookAndFeel::instance().fontNormal[DigitizerUi::LookAndFeel::instance().prototypeMode];
        window->DrawList->AddText(font, fontSize, textCursorInitial, textColor, fullSymbolName.begin(), fullSymbolName.end());
        return selected;
    }
};

/// A node in a tree of namespaces (branches) + symbols (leaf nodes). Built each frame based on the full typenames of registered blocks
struct NamespaceOrSymbol {
    struct NamespaceTag {};

    struct Namespace {
        std::unordered_map<std::string_view, std::unique_ptr<NamespaceOrSymbol>> children;
    };

    NamespaceOrSymbol() = delete;
    explicit NamespaceOrSymbol(const Symbol& symbol) : variant(symbol) {}
    explicit NamespaceOrSymbol(NamespaceTag) : variant(Namespace{}) {}

    NamespaceOrSymbol* insertOrFindNameSpaceChild(std::string_view key) { //
        return nameSpace()->children.try_emplace(key, std::make_unique<NamespaceOrSymbol>(NamespaceTag{})).first->second.get();
    }
    void insertOrFindElementChild(std::string_view key, std::string_view element, const FilterResult& filter) { //
        nameSpace()->children.try_emplace(key, std::make_unique<NamespaceOrSymbol>(Symbol{element, filter}));
    }

    /// If nonempty, the returned string view is the full symbol name of the selected symbol
    [[nodiscard]] std::string_view drawTreeRecursive(std::string_view currentlySelected, std::string& keyBuffer, std::optional<bool> newOpenValue) const {
        std::string_view selected;
        assert(nameSpace());

        for (const auto& [key, child] : nameSpace()->children) {
            const auto namespaceCase = [&keyBuffer, key, &newOpenValue, &child, &selected, currentlySelected](const Namespace&) {
                // this is a branch
                keyBuffer = key;
                DigitizerUi::IMW::ChangeStrId id(keyBuffer.c_str());
                if (newOpenValue) {
                    ImGui::SetNextItemOpen(*newOpenValue);
                }
                if (ImGui::TreeNodeEx(keyBuffer.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DrawLinesFull)) {
                    if (auto maybeSelected = child->drawTreeRecursive(currentlySelected, keyBuffer, newOpenValue); !maybeSelected.empty()) {
                        selected = maybeSelected;
                    }
                    ImGui::TreePop();
                }
            };
            const auto symbolCase = [currentlySelected, &selected](Symbol& symbol) {
                if (symbol.draw(currentlySelected)) {
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
} // namespace

namespace DigitizerUi {
void NewBlockSelector::drawNamespaceTree(const ImVec2& size) {
    IMW::Child window("##blockSelector", size, ImGuiChildFlags_None, ImGuiWindowFlags_None);

    constexpr const char* clearButtonText  = "Clear";
    constexpr const char* foldButtonText   = "Fold all";
    constexpr const char* unfoldButtonText = "Unfold all";
    const auto            originalCursorX  = ImGui::GetCursorPosX();
    const auto            buttonStart      = ImGui::GetContentRegionAvail().x - (IMW::CalcAdjacentButtonSizes(std::array{clearButtonText, foldButtonText, unfoldButtonText})).x;
    ImGui::SetCursorPosX(buttonStart);
    if (ImGui::Button(clearButtonText)) {
        m_blockFilter.clear();
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

    if (auto completionNamesView = data | std::views::keys; DigitizerUi::components::InputTextCompletion(completionNamesView).inputText("##filterBlockType", &m_blockFilter)) {
        newUnfoldedValue = true; // user typed in the input filter, they probably want to see all matches
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
            result.emplace_back(iterator, filterBlock(blockType, m_blockFilter));
        }

        if (this->m_blockFilter.empty()) {
            return result;
        }

        // sort by relevance to the filter query
        std::ranges::sort(result, std::ranges::greater(), [](const FilteredEntry& entry) { return entry.filterResult->score; });

        // remove all but roughly what will fit on the screen
        const auto maxEntries = static_cast<std::size_t>(std::ceilf((ImGui::GetContentRegionAvail().y / ImGui::GetTextLineHeightWithSpacing()) / 2.f));
        if (result.size() > maxEntries) {
            result.resize(maxEntries);
        }

        return result;
    }();

    // build a tree of namespace -> ... -> symbol
    auto globalNamespace = std::make_unique<NamespaceOrSymbol>(NamespaceOrSymbol::NamespaceTag{});
    for (const FilteredEntry& entry : filterResults | std::views::reverse) {
        NamespaceOrSymbol* cursor = globalNamespace.get();
        using namespace std::literals;
        for (const auto& namespaceElement : entry.blockTypeName() | std::views::split("::"sv)) {
            if (namespaceElement.end() == entry.blockTypeName().end()) { // last element in thing::thing::thing chain
                cursor->insertOrFindElementChild(std::string_view{namespaceElement}, entry.blockTypeName(), *entry.filterResult);
            } else {
                cursor = cursor->insertOrFindNameSpaceChild(std::string_view{namespaceElement});
            }
        }
    }

    // draw the tree
    std::string tempKeyBuffer;
    auto        newSelected = globalNamespace->drawTreeRecursive(this->m_currentlySelectedType, tempKeyBuffer, newUnfoldedValue);
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
        return;
    }

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

    if (components::DialogButtons() == components::DialogButton::Ok) {
        if (!m_currentlySelectedType.empty() && selectedParametrization) {
            gr::Message message;
            std::string type    = std::format("{}{}", m_currentlySelectedType, selectedParametrization.value_or({}));
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
