#include "StatusBarView.hpp"
#include "LogHistory.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <iterator>
#include <string>

#include <imgui_internal.h>

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

using Records = std::vector<gr::log::LogRecord>;

ImGuiID selectionIdOf(const gr::log::LogRecord& record) { return ImHashData(&record.timestampNanos, sizeof(record.timestampNanos)); }

ImGuiID selectionIdAt(ImGuiSelectionBasicStorage* selection, int index) { return selectionIdOf((*static_cast<const Records*>(selection->UserData))[static_cast<std::size_t>(index)]); }

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

    const float logAndBlocksWidth = .9f * ImGui::GetContentRegionAvail().x;
    drawLogLine(std::max(1.f, logAndBlocksWidth - _blocksWidth - ImGui::GetStyle().ItemSpacing.x));
    drawLogPopup();

    _blocksWidth = 0.f;
    if (session != nullptr && !_blocks.of(*session).empty()) {
        ImGui::SameLine();
        {
            IMW::Group blocks;
            for (const auto& block : _blocks.of(*session)) {
                IMW::ChangeStrId id(std::string(block->uniqueName()).c_str());
                std::ignore = block->draw();
                ImGui::SameLine();
            }
        }
        _blocksWidth = ImGui::GetItemRectSize().x;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(version.c_str()).x));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) * ImVec4(1.f, 1.f, 1.f, .6f), "%s", version.c_str());
    if (!versionDetails.empty()) {
        ImGui::SetItemTooltip("%s", versionDetails.c_str());
    }
}

bool StatusBarView::isInfoDotLit(std::uint64_t latestInfoOrDebugNanos, std::uint64_t nowNanos) noexcept { return nowNanos < latestInfoOrDebugNanos + static_cast<std::uint64_t>(std::chrono::nanoseconds(kInfoDotLifetime).count()); }

void StatusBarView::drawLogLine(float width) {
    const auto          counts   = logHistory().counts();
    const auto          latest   = logHistory().latestWarningOrWorse();
    const std::uint64_t errors   = counts[static_cast<std::size_t>(Level::fatal)] + counts[static_cast<std::size_t>(Level::failure)] + counts[static_cast<std::size_t>(Level::error)];
    const std::uint64_t warnings = counts[static_cast<std::size_t>(Level::warning)];
    const std::uint64_t others   = counts[static_cast<std::size_t>(Level::info)] + counts[static_cast<std::size_t>(Level::debug)] + counts[static_cast<std::size_t>(Level::trace)];
    const auto          now      = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

    const float startX   = ImGui::GetCursorPosX();
    const auto  levelDot = [](const char* id, bool isLit, ImVec4 colour, std::uint64_t count) {
        const float radius = ImGui::GetFontSize() * .3f;
        ImGui::InvisibleButton(id, ImVec2(2.f * radius, ImGui::GetFrameHeight()));
        if (ImGui::IsItemClicked()) {
            ImGui::OpenPopup(kLogPopup);
        }
        const ImVec2 centre = (ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * .5f;
        if (isLit) {
            ImGui::GetWindowDrawList()->AddCircleFilled(centre, radius, ImGui::ColorConvertFloat4ToU32(colour));
        } else {
            ImGui::GetWindowDrawList()->AddCircle(centre, radius, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) * ImVec4(1.f, 1.f, 1.f, .3f)));
        }
        ImGui::SetItemTooltip("%llu", static_cast<unsigned long long>(count));
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    };
    levelDot("##errorDot", errors > 0U, LookAndFeel::instance().palette().errorColor, errors);
    levelDot("##warningDot", warnings > 0U, kAmber, warnings);
    levelDot("##infoDot", isInfoDotLit(logHistory().latestInfoOrDebugNanos(), now), kGreen, others);

    const std::string label = std::format("{}###statusLine", latest ? textOf(*latest) : std::string_view{});
    {
        IMW::StyleColor colour(ImGuiCol_Text, latest ? colourOf(latest->level) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(std::max(1.f, width - (ImGui::GetCursorPosX() - startX)), 0.f))) {
            ImGui::OpenPopup(kLogPopup);
        }
    }
    if (latest) {
        ImGui::SetItemTooltip("%s:%u", latest->location, latest->line);
    }
}

std::string StatusBarView::selectedOrAllAsText(const std::vector<gr::log::LogRecord>& records) const {
    std::string text;
    for (const gr::log::LogRecord& record : records) {
        if (_selectedRecords.Size == 0 || _selectedRecords.Contains(selectionIdOf(record))) {
            std::format_to(std::back_inserter(text), "{}\t{}\t{}\t{}:{}\n", timeOfDay(record.timestampNanos), nameOf(record.level), std::string_view(record.text, record.textLength), std::string_view(record.location, record.locationLength), record.line);
        }
    }
    return text;
}

void StatusBarView::drawLogPopup() {
    const ImVec2 barTopLeft = ImGui::GetWindowPos();
    const float  barWidth   = ImGui::GetWindowWidth();
    ImGui::SetNextWindowPos(barTopLeft, ImGuiCond_Always, ImVec2(0.f, 1.f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(barWidth, 0.f), ImVec2(barWidth, ImGui::GetMainViewport()->Size.y * .6f));
    IMW::Popup popup(kLogPopup, 0);
    if (!popup) {
        return;
    }
    Records records                          = logHistory().snapshot();
    _selectedRecords.UserData                = &records;
    _selectedRecords.AdapterIndexToStorageId = selectionIdAt;
    if (ImGui::Button("Clear")) {
        logHistory().clear();
        _selectedRecords.Clear();
    }
    ImGui::SameLine();
    if (ImGui::Button(_selectedRecords.Size > 0 ? "Copy selected###copyLog" : "Copy all###copyLog") || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
        ImGui::SetClipboardText(selectedOrAllAsText(records).c_str());
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
    _selectedRecords.ApplyRequests(ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_BoxSelect1d | ImGuiMultiSelectFlags_ClearOnClickVoid, _selectedRecords.Size, static_cast<int>(records.size())));
    for (std::size_t index = 0UZ; index < records.size(); ++index) {
        const gr::log::LogRecord& record = records[index];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemSelectionUserData(static_cast<ImGuiSelectionUserData>(index));
        ImGui::Selectable(std::format("{}###record{}", timeOfDay(record.timestampNanos), index).c_str(), _selectedRecords.Contains(selectionIdOf(record)), ImGuiSelectableFlags_SpanAllColumns);
        ImGui::TableNextColumn();
        ImGui::TextColored(colourOf(record.level), "%.*s", static_cast<int>(nameOf(record.level).size()), nameOf(record.level).data());
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%.*s", static_cast<int>(record.textLength), record.text);
        ImGui::TableNextColumn();
        if (record.locationLength > 0U) {
            ImGui::TextDisabled("%s:%u", record.location, record.line);
        }
    }
    _selectedRecords.ApplyRequests(ImGui::EndMultiSelect());
}

} // namespace DigitizerUi
