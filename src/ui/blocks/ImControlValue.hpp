#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLVALUE_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLVALUE_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <mutex>
#include <string>

namespace opendigitizer {

GR_REGISTER_BLOCK(opendigitizer::ImControlValue, [T], [ float, double ])

template<typename T>
struct ImControlValue : gr::Block<ImControlValue<T>, gr::Drawable<gr::UICategory::Toolbar, "ImGui">> {
    using Description = gr::Doc<"UI display of the last value its asynchronous input received: 'label: value unit', the number right-aligned in `width` digits">;

    gr::PortIn<T, gr::Async> in;

    std::string                                                                                               label = "value";
    std::string                                                                                               unit;
    gr::Annotated<gr::Size_t, "width", gr::Doc<"digits the number is right-aligned in">, gr::Limits<1U, 32U>> width     = 8U;
    gr::Annotated<gr::Size_t, "precision", gr::Doc<"decimal places">, gr::Limits<0U, 17U>>                    precision = 1U;

    GR_MAKE_REFLECTABLE(ImControlValue, in, label, unit, width, precision);

    struct Shown { // the settings as draw() uses them: set on the scheduler thread, read on the UI thread
        std::string label = "value";
        std::string unit;
        gr::Size_t  width     = 8U;
        gr::Size_t  precision = 1U;
    };
    std::mutex        _shownMutex;
    Shown             _shown;
    std::atomic<T>    _last{};
    std::atomic<bool> _received{false};
    std::string       _drawnNumber;         // in the last draw, for tests
    float             _drawnRightGap = 0.f; // px between the number's right edge and its field's, in the last draw

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& /*newSettings*/) {
        std::lock_guard lock(_shownMutex);
        _shown = {.label = label, .unit = unit, .width = width.value, .precision = precision.value};
    }

    gr::work::Status processBulk(gr::InputSpanLike auto& inSpan) {
        if (inSpan.empty()) {
            return gr::work::Status::INSUFFICIENT_INPUT_ITEMS;
        }
        _last.store(inSpan.back(), std::memory_order_relaxed);
        _received.store(true, std::memory_order_release);
        std::ignore = inSpan.consume(inSpan.size());
        return gr::work::Status::OK;
    }

    gr::work::Status draw(const gr::property_map& /*config*/ = {}) {
        const Shown shown = [this] {
            std::lock_guard lock(_shownMutex);
            return _shown;
        }();
        const float spacing    = ImGui::GetStyle().ItemInnerSpacing.x;
        const float fieldWidth = static_cast<float>(shown.width) * ImGui::CalcTextSize("0").x;
        _drawnNumber           = _received.load(std::memory_order_acquire) ? std::format("{:.{}f}", _last.load(std::memory_order_relaxed), static_cast<int>(shown.precision)) : std::string("-");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(std::format("{}:", shown.label).c_str());
        ImGui::SameLine(0.f, spacing);
        const ImVec2 field       = ImGui::GetCursorScreenPos();
        const float  numberWidth = ImGui::CalcTextSize(_drawnNumber.c_str()).x;
        ImGui::SetCursorScreenPos(ImVec2(field.x + std::max(0.f, fieldWidth - numberWidth), field.y));
        ImGui::TextUnformatted(_drawnNumber.c_str());
        _drawnRightGap = field.x + std::max(fieldWidth, numberWidth) - ImGui::GetItemRectMax().x;
        if (!shown.unit.empty()) {
            ImGui::SameLine(0.f, spacing);
            ImGui::TextUnformatted(shown.unit.c_str());
        }
        return gr::work::Status::OK;
    }
};

} // namespace opendigitizer

#endif // OPENDIGITIZER_UI_BLOCKS_IMCONTROLVALUE_HPP
