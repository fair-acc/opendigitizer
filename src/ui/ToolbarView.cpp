#include "ToolbarView.hpp"

#include <algorithm>

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

namespace DigitizerUi {

namespace {
constexpr inline float kHeight      = 36.f;
constexpr inline float kLeftPadding = 16.f;

void drawSchedulerControls(GraphSession& session) {
    using enum gr::lifecycle::State;
    if (!session) {
        return;
    }
    const gr::lifecycle::State state  = session.state();
    const auto                 button = [](const char* label, const char* tooltip, bool enabled) {
        IMW::Disabled disabled(!enabled);
        bool          pressed = false;
        {
            IMW::Font font(LookAndFeel::instance().fontIconsSolid);
            pressed = ImGui::Button(label);
        }
        ImGui::SetItemTooltip("%s", tooltip);
        ImGui::SameLine();
        return pressed;
    };
    if (button("\uf04b###schedulerPlay", "start or resume the flowgraph", state == STOPPED || state == IDLE || state == PAUSED)) {
        session.start();
    }
    if (button("\uf04c###schedulerPause", "pause the flowgraph", state == RUNNING)) {
        session.pause();
    }
    if (button("\uf04d###schedulerStop", "stop the flowgraph", state == RUNNING || state == PAUSED)) {
        session.stop();
    }
}
constexpr inline std::string_view kSchedulerControlsKey = "##schedulerControls";
} // namespace

std::vector<ToolbarSlot> planToolbar(std::span<const toolbar::ItemWidths> items, float rowWidth, float spacing, std::size_t maxRows) {
    const auto minimumOf = [](const toolbar::ItemWidths& item, bool withLabel) { return withLabel ? item.minimum : item.minimum - item.label; };
    const auto naturalOf = [](const toolbar::ItemWidths& item, bool withLabel) { return withLabel ? item.natural : item.natural - item.label; };
    const auto fillRows  = [&](bool withLabel) {
        std::vector<std::size_t> rowOf(items.size());
        std::size_t              row  = 0UZ;
        float                    used = 0.f;
        for (std::size_t i = 0UZ; i < items.size(); ++i) {
            const float width = minimumOf(items[i], withLabel);
            if (i > 0UZ && used + spacing + width > rowWidth) {
                ++row;
                used = width;
            } else {
                used = i == 0UZ ? width : used + spacing + width;
            }
            rowOf[i] = row;
        }
        return rowOf;
    };
    const auto assign = [&](const std::vector<std::size_t>& rowOf, bool withLabel) {
        std::vector<ToolbarSlot> slots(items.size());
        for (std::size_t first = 0UZ; first < items.size();) {
            std::size_t last       = first;
            float       sumMinimum = 0.f;
            float       sumNatural = 0.f;
            for (; last < items.size() && rowOf[last] == rowOf[first]; ++last) {
                sumMinimum += minimumOf(items[last], withLabel);
                sumNatural += naturalOf(items[last], withLabel);
            }
            const float free   = rowWidth - spacing * static_cast<float>(last - first - 1UZ) - sumMinimum;
            const float growth = sumNatural > sumMinimum ? std::clamp(free / (sumNatural - sumMinimum), 0.f, 1.f) : 1.f;
            for (std::size_t i = first; i < last; ++i) {
                const float minimum = minimumOf(items[i], withLabel);
                slots[i]            = {.row = rowOf[i], .width = minimum + growth * (naturalOf(items[i], withLabel) - minimum), .showLabel = withLabel};
            }
            first = last;
        }
        return slots;
    };

    if (items.empty()) {
        return {};
    }
    for (const bool withLabel : {true, false}) {
        if (const auto rowOf = fillRows(withLabel); rowOf.back() < maxRows) {
            return assign(rowOf, withLabel);
        }
    }
    auto rowOf = fillRows(false);
    std::ranges::for_each(rowOf, [maxRows](std::size_t& row) { row = std::min(row, maxRows - 1UZ); });
    return assign(rowOf, false);
}

void ToolbarView::draw(GraphSession& session, bool schedulerControls) {
    const auto& blocks = _blocks.of(session);
    if (blocks.empty() && !schedulerControls) {
        return;
    }

    IMW::Child toolbar("##Toolbar", ImVec2(ImGui::GetContentRegionAvail().x, kHeight * static_cast<float>(_rows)), false, ImGuiWindowFlags_NoScrollbar);
    const auto widthsOf = [this](const std::string& key) {
        const float fixed = _fixedWidths.contains(key) ? _fixedWidths.at(key) : 0.f;
        return toolbar::publishedWidths(key).value_or(toolbar::ItemWidths{.natural = fixed, .minimum = fixed, .label = 0.f});
    };
    std::vector<std::string> keys;
    if (schedulerControls) {
        keys.emplace_back(kSchedulerControlsKey);
    }
    std::ranges::transform(blocks, std::back_inserter(keys), [](const auto& block) { return std::string(block->uniqueName()); });
    std::vector<toolbar::ItemWidths> widths;
    std::ranges::transform(keys, std::back_inserter(widths), widthsOf);
    const std::vector<ToolbarSlot> slots = planToolbar(widths, ImGui::GetContentRegionAvail().x - kLeftPadding, ImGui::GetStyle().ItemSpacing.x);
    _rows                                = slots.empty() ? 1UZ : slots.back().row + 1UZ;

    const ImVec2 origin = ImGui::GetCursorPos();
    for (std::size_t i = 0UZ; i < keys.size(); ++i) {
        if (i == 0UZ || slots[i].row != slots[i - 1UZ].row) {
            ImGui::SetCursorPos(ImVec2(origin.x + kLeftPadding, origin.y + kHeight * static_cast<float>(slots[i].row)));
        } else {
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        if (keys[i] == kSchedulerControlsKey) {
            drawSchedulerControls(session);
        } else {
            std::ignore = blocks[i - (schedulerControls ? 1UZ : 0UZ)]->draw(gr::property_map{{"max_width", slots[i].width}, {"show_label", slots[i].showLabel}});
        }
        ImGui::EndGroup();
        if (!toolbar::publishedWidths(keys[i])) {
            _fixedWidths[keys[i]] = ImGui::GetItemRectSize().x;
        }
    }

    const ImVec2   pos       = ImGui::GetWindowPos();
    const ImVec2   size      = ImGui::GetWindowSize();
    const uint32_t lineColor = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().toolbarLineColor);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + size.y - 1.f), ImVec2(pos.x + size.x, pos.y + size.y - 1.f), lineColor);
}

} // namespace DigitizerUi
