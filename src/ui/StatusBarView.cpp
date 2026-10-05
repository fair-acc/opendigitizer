#include "StatusBarView.hpp"
#include "LogHistory.hpp"

#include <chrono>
#include <ctime>
#include <format>
#include <string>

#include "blocks/StatusBarBlock.hpp"
#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

namespace DigitizerUi {

namespace {
using gr::log::Level;

constexpr inline const char* kLogPopup = "##statusBarLog";

ImVec4 colourOf(Level level) {
    switch (level) {
    case Level::fatal:
    case Level::failure:
    case Level::error: return LookAndFeel::instance().palette().errorColor;
    case Level::warning: return kAmber;
    case Level::info: return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    case Level::debug:
    case Level::trace: return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    }
    return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

std::string_view nameOf(Level level) { return gr::meta::enumName(level).value_or("?"); }

std::string_view textOf(const gr::log::LogRecord& record) { return {record.text, record.textLength}; }

std::string timeOfDay(std::uint64_t timestampNanos) {
    const auto        timePoint = std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(timestampNanos)));
    const std::time_t seconds   = std::chrono::system_clock::to_time_t(timePoint);
    std::tm           local{};
    localtime_r(&seconds, &local);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timePoint.time_since_epoch()).count() % 1000;
    return std::format("{:02}:{:02}:{:02}.{:03}", local.tm_hour, local.tm_min, local.tm_sec, milliseconds);
}
} // namespace

float StatusBarView::height() noexcept { return ImGui::GetFrameHeightWithSpacing(); }

void StatusBarView::draw(GraphSession* session) {
    IMW::Child     bar("##StatusBar", ImVec2(ImGui::GetContentRegionAvail().x, height()), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2   pos       = ImGui::GetWindowPos();
    const uint32_t lineColor = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().toolbarLineColor);
    ImGui::GetWindowDrawList()->AddLine(pos, ImVec2(pos.x + ImGui::GetWindowWidth(), pos.y), lineColor);

    drawLogLine();
    drawLogPopup();

    if (session == nullptr) {
        return;
    }
    for (const auto& block : _blocks.of(*session)) {
        ImGui::SameLine();
        IMW::ChangeStrId id(std::string(block->uniqueName()).c_str());
        std::ignore = block->draw();
    }
}

void StatusBarView::drawLogLine() {
    const auto          counts   = logHistory().counts();
    const auto          latest   = logHistory().latestWarningOrWorse();
    const std::uint64_t errors   = counts[static_cast<std::size_t>(Level::fatal)] + counts[static_cast<std::size_t>(Level::failure)] + counts[static_cast<std::size_t>(Level::error)];
    const std::uint64_t warnings = counts[static_cast<std::size_t>(Level::warning)];
    const std::uint64_t others   = counts[static_cast<std::size_t>(Level::info)] + counts[static_cast<std::size_t>(Level::debug)] + counts[static_cast<std::size_t>(Level::trace)];

    const std::string label = std::format("E {}  W {}  I {}  {}###statusLine", errors, warnings, others, latest ? textOf(*latest) : std::string_view{});
    {
        IMW::StyleColor colour(ImGuiCol_Text, latest ? colourOf(latest->level) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(ImGui::GetContentRegionAvail().x * .6f, 0.f))) {
            ImGui::OpenPopup(kLogPopup);
        }
    }
    if (latest) {
        ImGui::SetItemTooltip("%s:%u", latest->location, latest->line);
    }
}

void StatusBarView::drawLogPopup() {
    const ImVec2 barTopLeft = ImGui::GetWindowPos();
    ImGui::SetNextWindowPos(barTopLeft, ImGuiCond_Appearing, ImVec2(0.f, 1.f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(400.f, 0.f), ImVec2(ImGui::GetMainViewport()->Size.x, ImGui::GetMainViewport()->Size.y * .6f));
    IMW::Popup popup(kLogPopup, 0);
    if (!popup) {
        return;
    }
    if (ImGui::Button("Clear")) {
        logHistory().clear();
    }
    if (const std::uint64_t dropped = logHistory().dropped(); dropped > 0U) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%llu records not kept: logged while the history was read)", static_cast<unsigned long long>(dropped));
    }
    IMW::Table table("##statusBarRecords", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY, ImVec2(0.f, ImGui::GetMainViewport()->Size.y * .5f), 0.f);
    if (!table) {
        return;
    }
    ImGui::TableSetupColumn("time");
    ImGui::TableSetupColumn("level");
    ImGui::TableSetupColumn("message", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("source");
    ImGui::TableHeadersRow();
    for (const gr::log::LogRecord& record : logHistory().snapshot()) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(timeOfDay(record.timestampNanos).c_str());
        ImGui::TableNextColumn();
        ImGui::TextColored(colourOf(record.level), "%.*s", static_cast<int>(nameOf(record.level).size()), nameOf(record.level).data());
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%.*s", static_cast<int>(record.textLength), record.text);
        ImGui::TableNextColumn();
        if (record.locationLength > 0U) {
            ImGui::TextDisabled("%s:%u", record.location, record.line);
        }
    }
}

} // namespace DigitizerUi
