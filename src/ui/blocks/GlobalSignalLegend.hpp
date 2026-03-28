#ifndef OPENDIGITIZER_GLOBAL_SIGNAL_LEGEND_HPP
#define OPENDIGITIZER_GLOBAL_SIGNAL_LEGEND_HPP

#include <functional>
#include <string>
#include <string_view>

#include <gnuradio-4.0/Block.hpp>

#include "../charts/Chart.hpp"
#include "../charts/SinkRegistry.hpp"
#include "../common/ImguiWrap.hpp"

namespace DigitizerUi {

/// Global signal legend displaying all registered sinks from SinkRegistry.
/// Supports left-click toggle, right-click colour/settings, drag to charts, and drop from charts.
struct GlobalSignalLegend : gr::Block<GlobalSignalLegend, gr::Drawable<gr::UICategory::Toolbar, "ImGui">> {
    using RightClickCallback = std::function<void(std::string_view sinkUniqueName)>;

    enum class ClickResult { None, Left, RightColor, RightName };

    GR_MAKE_REFLECTABLE(GlobalSignalLegend);

    ImVec2             _legendSize{0.f, 0.f};
    float              _paneWidth{800.f};
    RightClickCallback _onRightClick;
    std::string        _editingSinkUniqueName;
    bool               _openColorPopup  = false;
    bool               _dragDropEnabled = true;

    void setRightClickCallback(RightClickCallback callback) { _onRightClick = std::move(callback); }
    void setPaneWidth(float width) { _paneWidth = width; }
    void setDragDropEnabled(bool isNowEnabled) { _dragDropEnabled = isNowEnabled; }

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        if (auto it = config.find("paneWidth"); it != config.end()) {
            if (auto* val = it->second.get_if<float>()) {
                _paneWidth = *val;
            }
        }
        _legendSize = drawLegend(_paneWidth);
        return gr::work::Status::OK;
    }

    [[nodiscard]] ImVec2 legendSize() const noexcept { return _legendSize; }

    static ClickResult drawLegendItem(std::uint32_t color, std::string_view text, bool enabled = true) {
        using namespace opendigitizer::charts;
        ClickResult                result = ClickResult::None;
        constexpr ImGuiMouseButton kRMB   = ImGuiMouseButton_Right;

        const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        const ImVec2 rectSize(ImGui::GetTextLineHeight() - 4, ImGui::GetTextLineHeight());

        // colour indicator square
        ImGui::GetWindowDrawList()->AddRectFilled(cursorPos + ImVec2(0, 2), cursorPos + rectSize - ImVec2(0, 2), rgbToImGuiABGR(color));

        if (ImGui::InvisibleButton("##ColorBox", rectSize)) {
            result = ClickResult::Left;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (ImGui::IsMouseReleased(kRMB)) {
                result = ClickResult::RightColor;
            }
        }
        ImGui::SameLine();

        // signal name button with transparent background
        ImVec2 buttonSize(rectSize.x + ImGui::CalcTextSize(text.data()).x - 4, ImGui::GetTextLineHeight());

        IMW::StyleColor buttonStyle(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        IMW::StyleColor hoveredStyle(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.1f));
        IMW::StyleColor activeStyle(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.2f));
        IMW::StyleColor textStyle(ImGuiCol_Text, enabled ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        if (ImGui::Button(text.data(), buttonSize)) {
            result = ClickResult::Left;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (ImGui::IsMouseReleased(kRMB)) {
                result = ClickResult::RightName;
            }
        }

        return result;
    }

    void drawSink(opendigitizer::SignalSink& sink, float& accumulatedWidth, float paneWidth, int index) {
        using namespace opendigitizer::charts;
        IMW::ChangeId itemId(index);

        const auto        color = sink.color();
        const std::string label = sink.signalName().empty() ? std::string(sink.name()) : std::string(sink.signalName());

        // check if we need to wrap to next line
        const auto widthEstimate = ImGui::CalcTextSize(label.c_str()).x + 20.f;
        if ((accumulatedWidth + widthEstimate) < 0.9f * paneWidth) {
            ImGui::SameLine();
        } else {
            accumulatedWidth = 0.f;
        }

        // draw the legend item
        auto clickResult = drawLegendItem(color, label, sink.drawEnabled());
        if (ImGui::IsItemHovered() && !ImGui::GetDragDropPayload()) {
            ImGui::SetTooltip(_dragDropEnabled ? "Drag/Toggle Signal" : "Toggle Signal");
        }

        if (clickResult == ClickResult::RightColor) {
            _editingSinkUniqueName = std::string(sink.uniqueName());
            _openColorPopup        = true;
        }
        if (clickResult == ClickResult::RightName) {
            if (_onRightClick) {
                _onRightClick(sink.uniqueName());
            }
        }
        if (clickResult == ClickResult::Left) {
            sink.setDrawEnabled(!sink.drawEnabled());
        }

        accumulatedWidth += ImGui::GetItemRectSize().x;

        // drag source - from global legend (empty source_chart_id = no removal needed)
        if (_dragDropEnabled) {
            if (auto dndSource = IMW::DragDropSource(ImGuiDragDropFlags_None)) {
                dnd::Payload payload{};
                opendigitizer::charts::dnd::copyToBuffer(payload.sink_name, label);
                ImGui::SetDragDropPayload(dnd::kPayloadType, &payload, sizeof(payload));
                drawLegendItem(color, label, sink.drawEnabled());
            }
        }
    }

    ImVec2 drawLegend(float paneWidth) {
        using namespace opendigitizer::charts;
        ImVec2 legendSize{0.f, 0.f};
        float  accumulatedWidth = paneWidth; // start at full width to force new line

        _openColorPopup = false;

        {
            IMW::Group group;

            int index = 0;
            SinkRegistry::instance().forEach([&](opendigitizer::SignalSink& sink) { drawSink(sink, accumulatedWidth, paneWidth, index++); });
        }

        legendSize.x = ImGui::GetItemRectSize().x;
        legendSize.y = std::max(5.f, ImGui::GetItemRectSize().y);

        // colour-only popup (right-click on colour square)
        if (_openColorPopup) {
            ImGui::OpenPopup("SinkColorPopup");
        }
        if (ImGui::BeginPopup("SinkColorPopup")) {
            auto editingSink = SinkRegistry::instance().findSink([this](const auto& s) { return s.uniqueName() == _editingSinkUniqueName; });
            if (editingSink) {
                std::uint32_t c   = editingSink->color();
                ImVec4        col = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(c));
                float         rgb[3]{col.x, col.y, col.z};
                if (ImGui::ColorPicker3("##color", rgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_PickerHueBar)) {
                    auto newColor = (static_cast<std::uint32_t>(rgb[0] * 255.0f) << 16) | (static_cast<std::uint32_t>(rgb[1] * 255.0f) << 8) | static_cast<std::uint32_t>(rgb[2] * 255.0f);
                    editingSink->setColor(newColor);
                }
            }
            ImGui::EndPopup();
        }

        // drop target - accept drops from charts, signal removal via dnd::g_state
        dnd::handleLegendDropTarget();

        return legendSize;
    }
};

} // namespace DigitizerUi

// register GlobalSignalLegend with the GR4 block registry
GR_REGISTER_BLOCK("DigitizerUi::GlobalSignalLegend", DigitizerUi::GlobalSignalLegend)
inline auto registerGlobalSignalLegend = gr::registerBlock<DigitizerUi::GlobalSignalLegend>(gr::globalBlockRegistry());

#endif // OPENDIGITIZER_GLOBAL_SIGNAL_LEGEND_HPP
