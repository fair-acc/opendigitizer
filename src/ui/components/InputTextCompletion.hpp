#ifndef OPENDIGITIZER_UI_COMPONENTS_INPUT_TEXT_COMPLETION_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_INPUT_TEXT_COMPLETION_HPP_

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <limits>
#include <ranges>
#include <vector>

namespace DigitizerUi::components {

template<typename T>
concept CompletionNamesCollection = !std::is_const_v<T> && std::ranges::forward_range<T> && std::is_convertible_v<std::ranges::range_value_t<T>, std::string_view>;

/// Struct containing params to pass to ImGui::InputText. Intended use:
/// InputTextCompletion(namesContainer}.inputText(...))
template<CompletionNamesCollection Collection>
struct InputTextCompletionContext {
    auto inputText(const char* label, std::string* buffer, ImGuiInputTextFlags flags = ImGuiInputTextFlags_None) { //
        return ImGui::InputText(label, buffer, flags | ImGuiInputTextFlags_CallbackCompletion, callback, data);
    }

    static int inputTextCompletionCallback(ImGuiInputTextCallbackData* d) {
        assert(d->UserData && "no userdata passed to ImGui::InputText()?");
        if (d->EventKey == ImGuiKey_Tab) {
            std::vector<std::string> candidates;
            std::size_t              shortest = std::numeric_limits<std::size_t>::max();

            auto notEmpty = [](const auto& svConvertible) { return !std::string_view{svConvertible}.empty(); };
            // sonarqube reports the next line as "object backing the pointer will be destroyed at the end of the full-expression"
            for (std::string_view name : *static_cast<Collection*>(d->UserData) | std::views::filter(notEmpty)) { // NOSONAR
                if (name.starts_with(std::string_view(d->Buf, static_cast<std::size_t>(d->BufTextLen)))) {
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

    Collection*            data;
    ImGuiInputTextCallback callback = &inputTextCompletionCallback;
};

template<CompletionNamesCollection Collection>
InputTextCompletionContext<Collection> InputTextCompletion(Collection& collection) {
    return {std::addressof(collection)};
}

} // namespace DigitizerUi::components

#endif
