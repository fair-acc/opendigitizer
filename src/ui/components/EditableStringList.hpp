#ifndef OPENDIGITIZER_UI_COMPONENTS_EDITABLE_STRING_LIST_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_EDITABLE_STRING_LIST_HPP_

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace DigitizerUi::components {

struct EditableStringList {
    std::vector<std::string> entries{std::string{}};

    bool _focusLastEntry = false;

    void draw(const char* strId) {
        constexpr const char* kIconPlus  = "\u{f067}";
        constexpr const char* kIconTrash = "\u{f2ed}";

        IMW::ChangeStrId listId(strId);
        const ImVec2     squareButtonSize{ImGui::GetFrameHeight(), ImGui::GetFrameHeight()};
        const float      inputWidth = ImGui::GetFontSize() * 12.f;

        std::optional<std::size_t> rowToDelete;
        for (std::size_t row = 0UZ; row < entries.size(); ++row) {
            IMW::ChangeId rowId(static_cast<int>(row));
            const bool    isLastRow = row + 1UZ == entries.size();
            if (isLastRow && _focusLastEntry) {
                ImGui::SetKeyboardFocusHere();
                _focusLastEntry = false;
            }
            ImGui::SetNextItemWidth(inputWidth);
            const bool enterPressed = ImGui::InputText("##entry", &entries[row], ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();

            if (isLastRow) {
                const bool canAddRow   = !entries[row].empty();
                bool       plusPressed = false;
                {
                    IMW::Disabled disabled(!canAddRow);
                    ImGui::PushFont(LookAndFeel::instance().fontIcons, squareButtonSize.y / 2.f);
                    plusPressed = ImGui::Button(kIconPlus, squareButtonSize);
                    ImGui::PopFont();
                }
                if (plusPressed || (enterPressed && canAddRow)) {
                    entries.emplace_back();
                    _focusLastEntry = true;
                }
            } else {
                ImGui::PushFont(LookAndFeel::instance().fontIcons, squareButtonSize.y / 2.f);
                if (ImGui::Button(kIconTrash, squareButtonSize)) {
                    rowToDelete = row;
                }
                ImGui::PopFont();
            }
        }
        if (rowToDelete.has_value()) {
            entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(*rowToDelete));
        }
    }

    [[nodiscard]] std::vector<std::string> nonEmptyEntries() const {
        std::vector<std::string> nonEmpty;
        std::ranges::copy_if(entries, std::back_inserter(nonEmpty), [](std::string_view entry) { return !entry.empty(); });
        return nonEmpty;
    }
};

} // namespace DigitizerUi::components

#endif
