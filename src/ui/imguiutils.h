#ifndef IMGUIUTILS_H
#define IMGUIUTILS_H

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "function_ref.hpp"

inline ImVec2 operator+(const ImVec2 a, const ImVec2 b) {
    ImVec2 r = a;
    r.x += b.x;
    r.y += b.y;
    return r;
}

inline ImVec2 operator-(const ImVec2 a, const ImVec2 b) {
    ImVec2 r = a;
    r.x -= b.x;
    r.y -= b.y;
    return r;
}

namespace ImGuiUtils {

namespace {
void ensureItemVisible() {
    auto scroll = ImGui::GetScrollY();
    auto min    = ImGui::GetWindowContentRegionMin().y + scroll;
    auto max    = ImGui::GetWindowContentRegionMax().y + scroll;

    auto h      = ImGui::GetItemRectSize().y;
    auto y      = ImGui::GetCursorPosY() - scroll;
    if (y > max) {
        ImGui::SetScrollHereY(1);
    } else if (y - h < min) {
        ImGui::SetScrollHereY(0);
    }
}

} // namespace

template<typename T, typename Items, typename ItemGetter, typename ItemDrawer>
std::optional<T> filteredListBox(const char *id, const ImVec2 &size, Items &&items, ItemGetter getItem, ItemDrawer drawItem) {
    ImGui::PushID(id);

    struct CallbackData {
        const Items &items;
        ItemGetter   getItem;
    } cbdata              = { items, getItem };

    auto completeItemName = [](ImGuiInputTextCallbackData *d) -> int {
        auto *cbdata = static_cast<CallbackData *>(d->UserData);
        if (d->EventKey == ImGuiKey_Tab) {
            std::vector<std::string> candidates;
            std::size_t              shortest = -1;
            for (auto &t : cbdata->items) {
                auto [item, name] = cbdata->getItem(t);
                if (name.empty()) {
                    continue;
                }
                if (name.starts_with(std::string_view(d->Buf, d->BufTextLen))) {
                    candidates.push_back(name);
                    shortest = std::min(shortest, name.size());
                }
            }

            if (candidates.empty()) {
                return 0;
            }

            for (auto &c : candidates) {
                c.resize(shortest);
            }

            while (candidates.size() > 1) {
                auto s = candidates.size();
                if (candidates[s - 2] == candidates[s - 1]) {
                    candidates.pop_back();
                    continue;
                }

                shortest--;
                assert(shortest > 0);
                for (auto &c : candidates) {
                    c.resize(shortest);
                }
            }
            auto *str = candidates.front().data();
            d->InsertChars(d->BufTextLen, &str[d->BufTextLen], &str[candidates.front().size()]);
        }
        return 0;
    };

    ImGui::BeginGroup();

    auto y = ImGui::GetCursorPosY();
    auto x = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Filter:");
    ImGui::SameLine();
    ImGui::SetCursorPosY(y);

    struct FilterListContext {
        std::optional<T> selected = {};
        std::string      filterString;
        std::vector<T>   filteredItems;
        bool             filterInputReclaimFocus = false;
    };
    auto  storage = ImGui::GetStateStorage();
    auto  ctxid   = ImGui::GetID("context");
    auto *ctx     = static_cast<FilterListContext *>(storage->GetVoidPtr(ctxid));
    if (!ctx) {
        ctx = new FilterListContext;
        storage->SetVoidPtr(ctxid, ctx);
    }

    if (ImGui::IsWindowAppearing() || ctx->filterInputReclaimFocus) {
        ImGui::SetKeyboardFocusHere();
        ctx->filterInputReclaimFocus = false;
    }
    ImGui::PushItemWidth(size.x - (ImGui::GetCursorPosX() - x));
    bool scrollToSelected = ImGui::InputText("##filterBlockType", &ctx->filterString, ImGuiInputTextFlags_CallbackCompletion, completeItemName, &cbdata);

    if (ImGui::BeginListBox("##Available Block types", size)) {
        auto filter = [&](std::string_view name) {
            if (!ctx->filterString.empty()) {
                auto it = std::search(name.begin(), name.end(), ctx->filterString.begin(), ctx->filterString.end(),
                        [](int a, int b) { return std::tolower(a) == std::tolower(b); });
                if (it == name.end()) {
                    return false;
                }
            }
            return true;
        };

        if (ctx->selected && !filter(ctx->selected->second)) {
            ctx->selected = {};
        }

        int selectOffset = 0;
        if (ctx->selected) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                ++selectOffset;
                scrollToSelected = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                --selectOffset;
                scrollToSelected = true;
            }
        }

        ctx->filteredItems.clear();
        for (auto &t : items) {
            auto [item, name] = getItem(t);
            if (!name.empty() && filter(name)) {
                ctx->filteredItems.push_back({ item, name });
            }
        }

        for (auto it = ctx->filteredItems.begin(); it != ctx->filteredItems.end(); ++it) {
            if (!ctx->selected) {
                ctx->selected = *it;
            }

            if (selectOffset == -1 && *(it + 1) == *ctx->selected) {
                ctx->selected = *it;
                selectOffset  = 0;
            } else if (selectOffset == 1 && *it == *ctx->selected && (it + 1) != ctx->filteredItems.end()) {
                ctx->selected = {};
                selectOffset  = 0;
            }

            if (drawItem(*it, ctx->selected && ctx->selected && *it == *ctx->selected)) {
                ctx->selected                = *it;
                ctx->filterInputReclaimFocus = true;
            }
            if (ctx->selected && *ctx->selected == *it && scrollToSelected) {
                ensureItemVisible();
            }
        }
        ImGui::EndListBox();
    }
    ImGui::EndGroup();

    ImGui::PopID();
    return ctx->selected;
}

template<typename Items, typename ItemGetter>
auto filteredListBox(const char *id, Items &&items, ItemGetter getItem, const ImVec2 &size = { 200, 200 })
    requires std::is_invocable_v<ItemGetter, decltype(*items.begin())>
{
    using T = decltype(getItem(*items.begin()));
    return filteredListBox<T>(id, size, items, getItem, [](auto &&item, bool selected) {
        return ImGui::Selectable(item.second.data(), selected);
    });
    ;
}

template<typename Items, typename ItemGetter, typename ItemDrawer>
auto filteredListBox(const char *id, Items &&items, ItemGetter getItem, ItemDrawer drawItem, const ImVec2 &size = { 200, 200 })
    requires std::is_invocable_v<ItemGetter, decltype(*items.begin())>
{
    using T = decltype(getItem(*items.begin()));
    return filteredListBox<T>(id, size, items, getItem, drawItem);
}

enum class DialogButton {
    None,
    Ok,
    Cancel
};

DialogButton drawDialogButton();

} // namespace ImGuiUtils

#endif
