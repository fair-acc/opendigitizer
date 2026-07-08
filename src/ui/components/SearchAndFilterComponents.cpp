#include "SearchAndFilterComponents.hpp"

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include "DateInput.hpp"
#include "FilterTagBar.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <ranges>
#include <utility>

namespace DigitizerUi::components {

namespace {
constexpr const char* kIconPlus   = "\u{f067}";
constexpr const char* kIconSearch = "\u{f002}";

constexpr std::array  kFilterDateLabels     = {"Before", "After"};
constexpr const char* kFilterDateComboLabel = "##filterDateDirection";

float calcFilterDateComboWidth() { //
    return IMW::CalcComboSize(kFilterDateComboLabel, kFilterDateLabels[0], ImGuiComboFlags_WidthFitPreview).preferred.x;
}

void drawVerticallyCenteredText(const char* label, float lineHeight) {
    const auto size = ImGui::CalcTextSize(label);
    assert(lineHeight >= size.y);
    auto oldPosition = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2{oldPosition.x, oldPosition.y + (lineHeight - size.y) / 2.f});
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2{ImGui::GetCursorScreenPos().x, oldPosition.y});
}

[[nodiscard]] bool drawPlusButton(bool enabled) {
    const ImVec2 squareButtonSize{ImGui::GetFrameHeight(), ImGui::GetFrameHeight()};

    IMW::Disabled disabled(!enabled);
    ImGui::PushFont(LookAndFeel::instance().fontIcons, squareButtonSize.y / 2.f);
    const bool pressed = ImGui::Button(kIconPlus, squareButtonSize);
    ImGui::PopFont();
    return pressed;
}

void drawRowLabel(const char* label) {
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
}
} // namespace

std::optional<std::string> TagFilterRow::draw(const char* strId, const char* label) {
    IMW::Font        font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);
    IMW::ChangeStrId rowId(strId);
    const float      rowWidth  = ImGui::GetContentRegionAvail().x;
    bool             addFilter = drawPlusButton(!pendingInput.empty());
    drawRowLabel(label);
    ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize(), rowWidth - (ImGui::GetCursorPosX() - ImGui::GetCursorStartPos().x)));
    addFilter |= ImGui::InputText("##tagFilterInput", &pendingInput, ImGuiInputTextFlags_EnterReturnsTrue);

    if (addFilter && !pendingInput.empty()) {
        return std::exchange(pendingInput, {});
    }
    return std::nullopt;
}

std::optional<KeyValueFilterRow::Committed> KeyValueFilterRow::draw(const char* strId) {
    IMW::Font        font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);
    IMW::ChangeStrId rowId(strId);
    const bool       hasKey = !pendingKey.empty();

    // two inputs share whatever the plus button, the two labels and the spacing between all five widgets leave over
    const float spacing     = ImGui::GetStyle().ItemSpacing.x;
    const float labelsWidth = ImGui::CalcTextSize("Key: ").x + ImGui::CalcTextSize("Value: ").x;
    const float fixedWidth  = ImGui::GetFrameHeight() + labelsWidth + 4.f * spacing;
    const float inputWidth  = std::max(ImGui::GetFontSize(), (ImGui::GetContentRegionAvail().x - fixedWidth) * 0.5f);

    bool addFilter = drawPlusButton(hasKey && !pendingValue.empty());
    drawRowLabel("Key: ");
    ImGui::SetNextItemWidth(inputWidth);
    addFilter |= ImGui::InputText("##keyValueFilterKeyInput", &pendingKey, ImGuiInputTextFlags_EnterReturnsTrue);
    {
        IMW::StyleColor labelColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(hasKey ? ImGuiCol_Text : ImGuiCol_TextDisabled));
        drawRowLabel("Value: ");
    }
    {
        IMW::Disabled disabled(!hasKey);
        ImGui::SetNextItemWidth(inputWidth);
        addFilter |= ImGui::InputText("##keyValueFilterValueInput", &pendingValue, ImGuiInputTextFlags_EnterReturnsTrue);
    }

    if (addFilter && hasKey && !pendingValue.empty()) {
        return Committed{.key = std::exchange(pendingKey, {}), .value = std::exchange(pendingValue, {})};
    }
    return std::nullopt;
}

std::optional<DateFilterRow::Committed> DateFilterRow::draw(const char* strId, const char* rowLabel) {
    IMW::Font        font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);
    IMW::ChangeStrId rowId(strId);

    std::optional<Committed> committed;
    if (drawPlusButton(true)) {
        committed = Committed{.direction = pendingDirection, .date = pendingDate};
    }
    drawRowLabel(rowLabel);

    ImGui::SetNextItemWidth(calcFilterDateComboWidth());
    if (auto combo = IMW::Combo(kFilterDateComboLabel, kFilterDateLabels[static_cast<std::size_t>(pendingDirection)], 0)) {
        using enum Direction;
        if (ImGui::Selectable(kFilterDateLabels[std::to_underlying(Before)])) {
            pendingDirection = Before;
        }
        if (ImGui::Selectable(kFilterDateLabels[std::to_underlying(After)])) {
            pendingDirection = After;
        }
    }

    ImGui::SameLine();
    if (const auto newDate = drawDateInput("##dateFilterDate", pendingDate)) {
        pendingDate = *newDate;
    }
    return committed;
}

float DateFilterRow::rowWidth(const char* rowLabel) {
    IMW::Font   font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);
    const float spacing      = ImGui::GetStyle().ItemSpacing.x;
    const float squareButton = ImGui::GetFrameHeight();
    return squareButton + spacing + ImGui::CalcTextSize(rowLabel).x + spacing + calcFilterDateComboWidth() + spacing + calcDateInputWidth();
}

std::optional<bool> drawMatchAnyOrAllFiltersCombo(const char* strId, bool requiresAll) {
    constexpr const char* matchAllLabel = "All filters";
    constexpr const char* matchAnyLabel = "Any filter";

    ImGui::SetNextItemWidth(IMW::CalcComboSize(strId, matchAllLabel, ImGuiComboFlags_None).preferred.x);
    bool matchAllFilters = requiresAll;
    if (auto combo = IMW::Combo(strId, matchAllFilters ? matchAllLabel : matchAnyLabel, ImGuiComboFlags_None)) {
        if (ImGui::Selectable(matchAllLabel)) {
            matchAllFilters = true;
        }
        if (ImGui::Selectable(matchAnyLabel)) {
            matchAllFilters = false;
        }
    }

    if (matchAllFilters != requiresAll) {
        return matchAllFilters;
    }
    return std::nullopt;
}

std::optional<std::size_t> drawSortByCombo(const char* strId, const char* rowLabel, std::span<const SortOption> options, std::size_t currentIndex) {
    assert(currentIndex < options.size());

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rowLabel);
    ImGui::SameLine();

    const auto  comboWidthFor = [strId](const SortOption& option) { return IMW::CalcComboSize(strId, option.label, ImGuiComboFlags_None).preferred.x; };
    const float comboWidth    = std::ranges::max(options | std::views::transform(comboWidthFor));
    ImGui::SetNextItemWidth(comboWidth);

    std::optional<std::size_t> chosen;
    if (auto combo = IMW::Combo(strId, options[currentIndex].label, ImGuiComboFlags_None)) {
        for (std::size_t optionIndex = 0UZ; optionIndex < options.size(); ++optionIndex) {
            IMW::Disabled disabled(!options[optionIndex].enabled);
            if (ImGui::Selectable(options[optionIndex].label, optionIndex == currentIndex)) {
                chosen = optionIndex;
            }
        }
    }
    return chosen;
}

SearchSortInput::Event SearchSortInput::draw(const char* strId, const char* hint, bool sortingBySearch) {
    IMW::Font font(LookAndFeel::instance().fontBig[LookAndFeel::instance().prototypeMode]);

    const float inputFrameHeight = ImGui::GetFrameHeight();
    {
        // TODO fix magnifying glass icon here, looks a bit weird, I think it may be more of an issue with the font
        IMW::Font iconFont(LookAndFeel::instance().fontIconsSolidBig);
        drawVerticallyCenteredText(kIconSearch, inputFrameHeight);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

    bool edited = false;
    {
        // grey out the search text while it is not the thing being sorted by
        IMW::StyleColor greyedOutText(ImGuiCol_Text, ImGui::GetStyleColorVec4(sortingBySearch ? ImGuiCol_Text : ImGuiCol_TextDisabled));
        edited = ImGui::InputTextWithHint(strId, hint, &text);
    }
    const bool searchInputActive = ImGui::IsItemActive();

    if (sortingBySearch && text.empty()) {
        return Event::wantsDefaultSort;
    }
    if (!text.empty() && (edited || (searchInputActive && !sortingBySearch))) {
        return Event::wantsRelevanceSort;
    }
    return Event::none;
}

std::optional<std::size_t> drawFilterTags(const char* strId, std::span<const std::unique_ptr<SortFilterModelFilter>> filters, const std::function<std::string(const SortFilterModelFilter&)>& labelFor) {
    if (filters.empty()) {
        return std::nullopt;
    }

    ImGui::Spacing();
    FilterTagBar tagBar(strId);

    std::size_t index = 0UZ;
    for (const auto& filter : filters) {
        tagBar.beginTag(index);
        ImGui::TextUnformatted(labelFor(*filter).c_str());
        tagBar.endTag();
        ++index;
    }
    return tagBar.finish();
}

} // namespace DigitizerUi::components
