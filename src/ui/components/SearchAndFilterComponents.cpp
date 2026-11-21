#include "SearchAndFilterComponents.hpp"

#include "../common/Imw.hpp"
#include "../common/LookAndFeel.hpp"
#include "StyledWidgets.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include "DateInput.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <optional>
#include <ranges>
#include <utility>

namespace gr::od::components {

namespace {
constexpr const char* kIconPlus   = "\u{f067}";
constexpr const char* kIconSearch = "\u{f002}";
constexpr const char* kIconClose  = "\u{f00d}";

constexpr std::array  kFilterDateLabels     = {"Before", "After"};
constexpr const char* kFilterDateComboLabel = "##filterDateDirection";

struct FilterTagBar {
    std::optional<std::size_t> _removed;
    std::size_t                _currentTag = 0UZ;
    std::size_t                _tagsDrawn  = 0UZ;
    bool                       _insideTag  = false;
    bool                       _finished   = false;

    explicit FilterTagBar(const char* strId) { ImGui::PushID(strId); }

    ~FilterTagBar() {
        if (!_finished) {
            static_cast<void>(finish());
        }
    }

    FilterTagBar(const FilterTagBar&)            = delete;
    FilterTagBar& operator=(const FilterTagBar&) = delete;

    void beginTag(std::size_t tagIndex) {
        assert(!_insideTag && !_finished);
        _insideTag  = true;
        _currentTag = tagIndex;

        if (_tagsDrawn > 0UZ) {
            ImGui::SameLine();
        }
        ImGui::PushID(static_cast<int>(tagIndex));

        // content goes on the foreground channel, endTag() fills the background afterwards
        ImGui::GetWindowDrawList()->ChannelsSplit(2);
        ImGui::GetWindowDrawList()->ChannelsSetCurrent(1);
        ImGui::BeginGroup();
        ImGui::AlignTextToFramePadding();
    }

    void endTag() {
        assert(_insideTag);
        ImGui::SameLine();
        {
            imw::StyleNamedColor transparentButton(ImGuiCol_Button, ImU32{0});
            imw::Font            iconFont(LookAndFeel::instance().fontIconsSolid);
            if (ImGui::Button(kIconClose)) {
                _removed = _currentTag;
            }
        }
        ImGui::EndGroup();

        auto*        drawList = ImGui::GetWindowDrawList();
        const ImRect tagRect{ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
        drawList->ChannelsSetCurrent(0);
        drawList->AddRectFilled(tagRect.Min, tagRect.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
        drawList->AddRect(tagRect.Min, tagRect.Max, ImGui::GetColorU32(ImGuiCol_Border), ImGui::GetStyle().FrameRounding);
        drawList->ChannelsMerge();

        ImGui::PopID();
        _insideTag = false;
        ++_tagsDrawn;
    }

    [[nodiscard]] std::optional<std::size_t> finish() {
        assert(!_insideTag && !_finished);
        _finished = true;
        ImGui::PopID();
        return std::exchange(_removed, {});
    }
};

float calcFilterDateComboWidth() { //
    return imw::calcComboSize(kFilterDateComboLabel, kFilterDateLabels[0], ImGuiComboFlags_WidthFitPreview).preferred.x;
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

    imw::Disabled disabled(!enabled);
    ImGui::PushFont(LookAndFeel::instance().fontIcons, squareButtonSize.y / 2.f);
    const bool pressed = button(kIconPlus, squareButtonSize);
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
    imw::Font        font(LookAndFeel::instance().fontSmall());
    imw::ChangeStrId rowId(strId);
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
    imw::Font        font(LookAndFeel::instance().fontSmall());
    imw::ChangeStrId rowId(strId);
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
        imw::StyleColor labelColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(hasKey ? ImGuiCol_Text : ImGuiCol_TextDisabled));
        drawRowLabel("Value: ");
    }
    {
        imw::Disabled disabled(!hasKey);
        ImGui::SetNextItemWidth(inputWidth);
        addFilter |= ImGui::InputText("##keyValueFilterValueInput", &pendingValue, ImGuiInputTextFlags_EnterReturnsTrue);
    }

    if (addFilter && hasKey && !pendingValue.empty()) {
        return Committed{.key = std::exchange(pendingKey, {}), .value = std::exchange(pendingValue, {})};
    }
    return std::nullopt;
}

std::optional<DateFilterRow::Committed> DateFilterRow::draw(const char* strId, const char* rowLabel) {
    imw::Font        font(LookAndFeel::instance().fontSmall());
    imw::ChangeStrId rowId(strId);

    std::optional<Committed> committed;
    if (drawPlusButton(true)) {
        committed = Committed{.direction = pendingDirection, .date = pendingDate};
    }
    drawRowLabel(rowLabel);

    ImGui::SetNextItemWidth(calcFilterDateComboWidth());
    if (auto combo = ComboBox(kFilterDateComboLabel, kFilterDateLabels[static_cast<std::size_t>(std::to_underlying(pendingDirection))], 0)) {
        using enum Direction;
        if (selectable(kFilterDateLabels[std::to_underlying(Before)])) {
            pendingDirection = Before;
        }
        if (selectable(kFilterDateLabels[std::to_underlying(After)])) {
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
    imw::Font   font(LookAndFeel::instance().fontSmall());
    const float spacing      = ImGui::GetStyle().ItemSpacing.x;
    const float squareButton = ImGui::GetFrameHeight();
    return squareButton + spacing + ImGui::CalcTextSize(rowLabel).x + spacing + calcFilterDateComboWidth() + spacing + calcDateInputWidth();
}

std::optional<bool> drawMatchAnyOrAllFiltersCombo(const char* strId, bool requiresAll) {
    constexpr const char* kMatchAllLabel = "All filters";
    constexpr const char* kMatchAnyLabel = "Any filter";

    ImGui::SetNextItemWidth(imw::calcComboSize(strId, kMatchAllLabel, ImGuiComboFlags_None).preferred.x);
    bool matchAllFilters = requiresAll;
    if (auto combo = ComboBox(strId, matchAllFilters ? kMatchAllLabel : kMatchAnyLabel, ImGuiComboFlags_None)) {
        if (selectable(kMatchAllLabel)) {
            matchAllFilters = true;
        }
        if (selectable(kMatchAnyLabel)) {
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

    const auto  comboWidthFor = [strId](const SortOption& option) { return imw::calcComboSize(strId, option.label, ImGuiComboFlags_None).preferred.x; };
    const float comboWidth    = std::ranges::max(options | std::views::transform(comboWidthFor));
    ImGui::SetNextItemWidth(comboWidth);

    std::optional<std::size_t> chosen;
    if (auto combo = ComboBox(strId, options[currentIndex].label, ImGuiComboFlags_None)) {
        for (std::size_t optionIndex = 0UZ; optionIndex < options.size(); ++optionIndex) {
            imw::Disabled disabled(!options[optionIndex].enabled);
            if (selectable(options[optionIndex].label, optionIndex == currentIndex)) {
                chosen = optionIndex;
            }
        }
    }
    return chosen;
}

SearchSortInput::Event SearchSortInput::draw(const char* strId, const char* hint, bool sortingBySearch, float trailingWidth) {
    imw::Font font(LookAndFeel::instance().fontBig());

    const float inputFrameHeight = ImGui::GetFrameHeight();
    {
        imw::Font iconFont(LookAndFeel::instance().fontIconsSolidBig);
        drawVerticallyCenteredText(kIconSearch, inputFrameHeight);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - trailingWidth);

    bool edited = false;
    {
        imw::StyleColor greyedOutText(ImGuiCol_Text, ImGui::GetStyleColorVec4(sortingBySearch ? ImGuiCol_Text : ImGuiCol_TextDisabled));
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

std::optional<std::size_t> drawFilterTags(const char* strId, std::span<const std::string> labels) {
    if (labels.empty()) {
        return std::nullopt;
    }

    ImGui::Spacing();
    FilterTagBar tagBar(strId);

    std::size_t index = 0UZ;
    for (const std::string& label : labels) {
        tagBar.beginTag(index);
        ImGui::TextUnformatted(label.c_str());
        tagBar.endTag();
        ++index;
    }
    return tagBar.finish();
}

} // namespace gr::od::components
