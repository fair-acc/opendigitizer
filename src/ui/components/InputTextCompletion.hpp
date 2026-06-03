#ifndef OPENDIGITIZER_UI_COMPONENTS_INPUT_TEXT_COMPLETION_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_INPUT_TEXT_COMPLETION_HPP_

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <limits>
#include <ranges>
#include <vector>

namespace DigitizerUi::components {

template<typename T>
concept CompletionNamesCollection = !std::is_const_v<T> && std::ranges::forward_range<T> && std::is_same_v<std::remove_cvref_t<std::ranges::range_value_t<T>>, std::string>;

/// Struct containing params to pass to ImGui::InputText. Intended use:
/// InputTextCompletion(namesContainer}.inputText(...))
template<CompletionNamesCollection Collection>
struct InputTextCompletionContext {
    static int inputTextCompletionCallback(ImGuiInputTextCallbackData* d) {
        assert(d->UserData && "no userdata passed to ImGui::InputText()?");
        if (d->EventKey == ImGuiKey_Tab) {
            std::vector<std::string> candidates;
            std::size_t              shortest         = std::numeric_limits<std::size_t>::max();
            std::string_view         inputBufferSoFar = {d->Buf, static_cast<std::size_t>(d->BufTextLen)};

            if (inputBufferSoFar.empty()) {
                // user has not typed anything yet so we don't know what they
                // want, and the rest of this function does not handle this case
                return 0;
            }

            auto notEmpty = [](const auto& string) { return !string.empty(); };
            for (const std::string& name : *static_cast<Collection*>(d->UserData) | std::views::filter(notEmpty)) {
                if (name.starts_with(inputBufferSoFar)) {
                    candidates.emplace_back(name);
                    shortest = std::min(shortest, name.size());
                }
            }

            if (candidates.empty()) {
                return 0;
            }

            for (auto& c : candidates) {
                c.resize(shortest);
            }

            while (candidates.size() > 1) {
                if (auto s = candidates.size(); candidates[s - 2] == candidates[s - 1]) {
                    candidates.pop_back();
                    continue;
                }

                shortest--;
                assert(shortest > 0);
                for (auto& c : candidates) {
                    c.resize(shortest);
                }
            }
            const char* str = candidates.front().data();
            d->InsertChars(d->BufTextLen, &str[d->BufTextLen], &str[candidates.front().size()]);
        }
        return 0;
    }

    Collection*            data     = nullptr;
    ImGuiInputTextCallback callback = &inputTextCompletionCallback;

    auto inputText(const char* label, std::string* buffer, ImGuiInputTextFlags flags = ImGuiInputTextFlags_None) { //
        return ImGui::InputText(label, buffer, flags | ImGuiInputTextFlags_CallbackCompletion, callback, data);
    }
};

template<CompletionNamesCollection Collection>
InputTextCompletionContext<Collection> InputTextCompletion(Collection& collection) {
    return {std::addressof(collection)};
}

} // namespace DigitizerUi::components

#endif
