#ifndef OPENDIGITIZER_UI_COMPONENTS_LIST_BOX_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_LIST_BOX_HPP_

#include "../common/ImguiWrap.hpp"
#include "tolower.hpp"

#include "InputTextCompletion.hpp"
#include <misc/cpp/imgui_stdlib.h>

#include <optional>
#include <vector>

namespace DigitizerUi::components {

namespace detail {
inline void ensureItemVisible() {
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

template<typename T>
struct FilterListContext {
    std::optional<T> selected = {};
    std::string      filterString;
    std::vector<T>   filteredItems;
    bool             filterInputReclaimFocus = false;

    [[nodiscard]] bool filter(std::string_view name) {
        if (!filterString.empty()) {
            auto subrange = std::ranges::search(name, filterString, [](char a, char b) { return Digitizer::utils::safe_tolower(a) == Digitizer::utils::safe_tolower(b); });
            if (std::begin(subrange) == name.end()) {
                return false;
            }
        }
        return true;
    };

    std::pair<int, bool> initSelection(bool shouldScrollToSelected) {
        if (selected && !filter(selected->second)) {
            selected = {};
        }

        if (selected) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                return {1, true};
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                return {-1, true};
            }
        }
        return {0, shouldScrollToSelected};
    }

    template<typename Items, typename ItemGetter, typename ItemDrawer>
    void draw(const Items& items, ItemGetter getItem, ItemDrawer drawItem, bool shouldScrollToSelected) {
        auto [selectOffset, scrollToSelected] = initSelection(shouldScrollToSelected);

        filteredItems.clear();
        for (auto& t : items) {
            auto [item, name] = getItem(t);
            if (!name.empty() && filter(name)) {
                filteredItems.push_back({item, name});
            }
        }

        for (auto it = filteredItems.begin(); it != filteredItems.end(); ++it) {
            if (!selected) {
                selected = *it;
            }

            const bool isLastIteration = (it + 1) == filteredItems.end();
            if (!isLastIteration) {
                if (selectOffset == -1 && *(it + 1) == *selected) { // selectOffset = -1, go up, ie. next item is selected and current should be instead
                    selected     = *it;
                    selectOffset = 0;
                } else if (selectOffset == 1 && *it == *selected) { // selectOffset = 1, go down, ie. this is selected and select next item
                    selected     = {};
                    selectOffset = 0;
                }
            }

            if (drawItem(*it, selected && *it == *selected)) {
                selected                = *it;
                filterInputReclaimFocus = true;
            }
            if (selected && *selected == *it && scrollToSelected) {
                detail::ensureItemVisible();
            }
        }
    }
};

} // namespace detail

template<typename T, typename Items, typename ItemGetter, typename ItemDrawer>
std::optional<T> FilteredListBox(const char* id, const ImVec2& size, const Items& items, ItemGetter getItem, ItemDrawer&& drawItem) {
    IMW::ChangeStrId newId(id);

    IMW::Group group;

    auto x = ImGui::GetCursorPosX();
    auto y = ImGui::GetCursorPosY();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Filter:");
    ImGui::SameLine();
    ImGui::SetCursorPosY(y);

    auto  storage = ImGui::GetStateStorage();
    auto  ctxid   = ImGui::GetID("context");
    auto* ctx     = static_cast<detail::FilterListContext<T>*>(storage->GetVoidPtr(ctxid));
    if (!ctx) {
        // this new will leak this object but that is okay, it and ImGui live for the whole program
        ctx = new detail::FilterListContext<T>; // NOSONAR
        storage->SetVoidPtr(ctxid, ctx);
    }

    if (ImGui::IsWindowAppearing() || ctx->filterInputReclaimFocus) {
        ImGui::SetKeyboardFocusHere();
        ctx->filterInputReclaimFocus = false;
    }

    IMW::ItemWidth newItemWidth(size.x - (ImGui::GetCursorPosX() - x));
    const bool     scrollToSelected = [&getItem, &items, ctx] {
        auto completionNames = items | std::views::transform([&getItem](auto& item) { return getItem(item).second; });
        return InputTextCompletion(completionNames).inputText("##filterBlockType", &ctx->filterString);
    }();

    if (auto listbox = IMW::ListBox("##Available Block types", ImVec2{size.x, size.y - (ImGui::GetCursorPosY() - y)})) {
        ctx->draw(items, std::move(getItem), std::forward<ItemDrawer>(drawItem), scrollToSelected);
    }

    return ctx->selected;
}

template<typename Items, typename ItemGetter>
auto FilteredListBox(const char* id, const ImVec2& size, const Items& items, ItemGetter&& getItem)
requires std::is_invocable_v<ItemGetter, decltype(*items.begin())>
{
    using T = decltype(getItem(*items.begin()));
    return FilteredListBox<T>(id, size, items, std::forward<ItemGetter>(getItem), [](auto&& item, bool selected) { return ImGui::Selectable(item.second.data(), selected); });
}

template<typename Items, typename ItemGetter, typename ItemDrawer>
auto FilteredListBox(const char* id, const ImVec2& size, const Items& items, ItemGetter&& getItem, ItemDrawer&& drawItem)
requires std::is_invocable_v<ItemGetter, decltype(*items.begin())>
{
    using T = decltype(getItem(*items.begin()));
    return FilteredListBox<T>(id, size, items, std::forward<ItemGetter>(getItem), std::forward<ItemDrawer>(drawItem));
}

} // namespace DigitizerUi::components

#endif
