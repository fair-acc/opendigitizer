#include "FilterTagBar.hpp"

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

#include <cassert>
#include <cstring>

namespace DigitizerUi::components {

namespace {
constexpr const char* kIconClose = "\u{f00d}";
} // namespace

FilterTagBar::FilterTagBar(const char* strId) { ImGui::PushID(strId); }

FilterTagBar::~FilterTagBar() {
    if (!_finished) {
        static_cast<void>(finish());
    }
}

void FilterTagBar::beginTag(std::size_t tagIndex) {
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

void FilterTagBar::endTag() {
    assert(_insideTag);
    ImGui::SameLine();
    {
        IMW::StyleNamedColor transparentButton(ImGuiCol_Button, ImU32{0});
        IMW::Font            iconFont(LookAndFeel::instance().fontIconsSolid);
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

std::optional<std::size_t> FilterTagBar::finish() {
    assert(!_insideTag && !_finished);
    _finished = true;
    ImGui::PopID();
    return std::exchange(_removed, {});
}

} // namespace DigitizerUi::components
