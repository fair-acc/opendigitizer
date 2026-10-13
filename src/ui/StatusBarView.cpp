#include "StatusBarView.hpp"
#include "LogHistory.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>
#include <iterator>
#include <string>
#include <system_error>

#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include "blocks/StatusBarBlock.hpp"
#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"
#include "components/ImGuiNotify.hpp"

#include "tolower.hpp"

using namespace std::string_literals;

namespace DigitizerUi {

namespace {
using gr::log::Level;

constexpr inline const char* kLogPopup              = "##statusBarLog";
constexpr inline const char* kSaveLogPopup          = "Save log to file##statusBarLogSave";
constexpr inline const char* kSaveLogOverwritePopup = "Overwrite file##statusBarLogOverwrite";

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

std::string_view logLevelName(Level level) { return gr::meta::enumName(level).value_or("?"); }

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

std::string singleLineText(std::string_view text) {
    std::string result(text);
    std::ranges::replace_if(result, [](char ch) { return ch == '\n' || ch == '\r' || ch == '\t'; }, ' ');
    return result;
}

constexpr inline std::array kLogLevelsComboData{Level::fatal, Level::failure, Level::error, Level::warning, Level::info, Level::debug, Level::trace};

std::string_view levelFilterName(std::optional<Level> levelFilter) { return levelFilter ? logLevelName(*levelFilter) : std::string_view{"All"}; }

Records filterRecords(const Records& records, std::optional<Level> levelFilter, std::string_view messageFilter) {
    Records filtered;
    std::copy_if(records.begin(), records.end(), std::back_inserter(filtered), [&](const gr::log::LogRecord& record) {
        const bool levelMatches = !levelFilter || record.level == *levelFilter;
        return levelMatches && Digitizer::utils::caseInsensitiveContains(textOf(record), messageFilter);
    });
    return filtered;
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

    const std::string renderedText = latest ? singleLineText(textOf(*latest)) : std::string{};
    const std::string label        = std::format("{}###statusLine", renderedText);
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

std::string StatusBarView::selectedOrAllAsText(const std::vector<gr::log::LogRecord>& records, bool selectedOnly) const {
    std::string text;
    for (const gr::log::LogRecord& record : records) {
        if (!selectedOnly || _selectedRecords.Contains(selectionIdOf(record))) {
            std::format_to(std::back_inserter(text), "{}\t{}\t{}\t{}:{}\n", timeOfDay(record.timestampNanos), logLevelName(record.level), std::string_view(record.text, record.textLength), std::string_view(record.location, record.locationLength), record.line);
        }
    }
    return text;
}

void StatusBarView::openSavePopup(std::string text) {
    _saving = SaveData{
        .pendingText = std::move(text),      //
        .path        = "opendigitizer.log"s, //
        .writerPath  = ""s,                  //
        .writer      = {}                    //
    };
    ImGui::OpenPopup(kSaveLogPopup);
}

bool StatusBarView::startSavePendingTextToFile() {
    if (!_saving) {
        return false;
    }

    std::vector<std::uint8_t> bytes(_saving->pendingText.begin(), _saving->pendingText.end());

    auto writerExp = gr::algorithm::fileio::writeAsync(_saving->path, bytes);
    if (!writerExp) {
        components::Notification::error(std::format("Cannot start write to '{}': {}", _saving->path, writerExp.error().message));
        return false;
    }

    _saving->writerPath = _saving->path;
    _saving->writer     = std::move(*writerExp);
    _saving->pendingText.clear();
    return true;
}

void StatusBarView::pollSaveResult() {
    if (!_saving || !_saving->writer || !_saving->writer->finished()) {
        return;
    }
    const auto result = _saving->writer->result();
    if (!result) {
        components::Notification::error(std::format("Failed to write '{}': {}", _saving->writerPath, result.error().message));
    } else {
        components::Notification::info(std::format("Saved log to '{}'", _saving->writerPath));
    }
    _saving.reset();
}

void StatusBarView::drawSavePopup() {
    ImGui::SetNextWindowSize({700.f, ImGui::GetFrameHeight() * 4.f}, ImGuiCond_Once);
    if (auto popup = IMW::ModalPopup(kSaveLogPopup, nullptr, 0)) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Path:");
        ImGui::SameLine();
        ImGui::InputText("##saveLogPath", &_saving->path);

        bool       closeSavePopup = false;
        const bool saveEnabled    = !_saving->path.empty();
        const bool saveBusy       = _saving->writer.has_value();

        ImGui::Separator();
        {
            IMW::Disabled disabled(!saveEnabled || saveBusy);
            if ((ImGui::Button("Save###saveLogConfirm") || (saveEnabled && ImGui::IsKeyPressed(ImGuiKey_Enter))) && saveEnabled && !saveBusy) {
                std::error_code error;
                const bool      exists = std::filesystem::exists(_saving->path, error);
                if (error) {
                    components::Notification::error(std::format("Cannot inspect '{}': {}", _saving->path, error.message()));
                } else if (exists) {
                    ImGui::OpenPopup(kSaveLogOverwritePopup);
                } else {
                    closeSavePopup = startSavePendingTextToFile();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel###saveLogCancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            _saving.reset();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SetNextWindowSize(ImGui::GetWindowSize(), ImGuiCond_Always);
        if (auto overwritePopup = IMW::ModalPopup(kSaveLogOverwritePopup, nullptr, 0)) {
            bool closeOverwritePopup = false;
            ImGui::TextWrapped("'%s' already exists. Overwrite it?", _saving->path.c_str());
            const bool acceptOverwrite = ImGui::Button("Overwrite###saveLogOverwrite") || (!ImGui::IsWindowAppearing() && ImGui::IsKeyPressed(ImGuiKey_Enter));
            if (acceptOverwrite && !_saving->writer.has_value()) {
                closeSavePopup      = startSavePendingTextToFile();
                closeOverwritePopup = closeSavePopup;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel###saveLogOverwriteCancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }
            if (closeOverwritePopup) {
                ImGui::CloseCurrentPopup();
            }
        }

        if (closeSavePopup) {
            ImGui::CloseCurrentPopup();
        }
    }
}

void StatusBarView::drawLogPopup() {
    pollSaveResult();

    struct ButtonText {
        constexpr explicit ButtonText(const char* _data) : data(_data) {
            assert(data != nullptr);
            std::string_view view(data);
            const auto       pos = view.find("###");
            textOnly             = pos == std::string_view::npos ? std::string(view) : std::string(view.substr(0, pos));
            std::println("Button registered data = {}, caption = {}", data, textOnly);
        }

        operator const char*() const { return data; }

        const char* data;
        std::string textOnly;
        auto        width() const { return ImGui::CalcTextSize(textOnly.c_str()).x; }
    };

    static const ButtonText clearLogsText("Clear logs###clearLogs");
    static const ButtonText closeText("Close###closeLogPopup");
    static const ButtonText copySelectedText("Copy selected###copyLog");
    static const ButtonText copyAllText("Copy all###copyLog");
    static const ButtonText saveSelectedText("Save selection to file###saveLogToFile");
    static const ButtonText saveAllText("Save to file###saveLogToFile");

    const ImVec2 barTopLeft = ImGui::GetWindowPos();
    const float  barWidth   = ImGui::GetWindowWidth();
    ImGui::SetNextWindowPos(barTopLeft, ImGuiCond_Always, ImVec2(0.f, 1.f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(barWidth, 0.f), ImVec2(barWidth, ImGui::GetMainViewport()->Size.y * .6f));
    IMW::Popup popup(kLogPopup, 0);
    if (!popup) {
        return;
    }

    const std::optional<Level> levelFilterBefore   = _levelFilter;
    const std::string          messageFilterBefore = _messageFilter;
    const ImVec2               buttonPadding       = ImGui::GetStyle().FramePadding;

    Records       records                    = logHistory().snapshot();
    const Records visibleRecords             = filterRecords(records, _levelFilter, _messageFilter);
    _selectedRecords.UserData                = const_cast<Records*>(&visibleRecords);
    _selectedRecords.AdapterIndexToStorageId = selectionIdAt;
    const bool hasVisibleSelection           = std::any_of(visibleRecords.begin(), visibleRecords.end(), [this](const gr::log::LogRecord& record) { return _selectedRecords.Contains(selectionIdOf(record)); });

    const float clearWidth            = clearLogsText.width() + 2.f * buttonPadding.x;
    const float closeWidth            = closeText.width() + 2.f * buttonPadding.x;
    const float copyWidth             = (hasVisibleSelection ? copySelectedText.width() : copyAllText.width()) + 2.f * buttonPadding.x;
    const float saveWidth             = (hasVisibleSelection ? saveSelectedText.width() : saveAllText.width()) + 2.f * buttonPadding.x;
    const float trailingControlsWidth = clearWidth + copyWidth + saveWidth + closeWidth + 4.f * ImGui::GetStyle().ItemSpacing.x;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(10.f * ImGui::GetFontSize());
    const std::string levelFilterPreview(levelFilterName(_levelFilter));
    if (ImGui::BeginCombo("##levelFilter", levelFilterPreview.c_str())) {
        if (ImGui::Selectable("All", !_levelFilter)) {
            _levelFilter.reset();
        }
        for (const Level level : kLogLevelsComboData) {
            const std::string label(logLevelName(level));
            if (ImGui::Selectable(label.c_str(), _levelFilter == level)) {
                _levelFilter = level;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x - trailingControlsWidth));
    ImGui::InputTextWithHint("##logTextFilter", "Filter messages", &_messageFilter);

    if (_levelFilter != levelFilterBefore || _messageFilter != messageFilterBefore) {
        _selectedRecords.Clear();
    }

    ImGui::SameLine();
    if (ImGui::Button(clearLogsText)) {
        logHistory().clear();
        _selectedRecords.Clear();
        records.clear();
    }

    ImGui::SameLine();
    if (ImGui::Button(hasVisibleSelection ? copySelectedText : copyAllText) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
        ImGui::SetClipboardText(selectedOrAllAsText(visibleRecords, hasVisibleSelection).c_str());
    }
    ImGui::SameLine();
    {
        IMW::Disabled disabled(_saving && _saving->writer.has_value());
        if (ImGui::Button(hasVisibleSelection ? saveSelectedText : saveAllText)) {
            openSavePopup(selectedOrAllAsText(visibleRecords, hasVisibleSelection));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(closeText)) {
        ImGui::CloseCurrentPopup();
    }
    drawSavePopup();

    if (const std::uint64_t dropped = logHistory().dropped(); dropped > 0U) {
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
    _selectedRecords.ApplyRequests(ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_BoxSelect1d | ImGuiMultiSelectFlags_ClearOnClickVoid, _selectedRecords.Size, static_cast<int>(visibleRecords.size())));
    for (std::size_t index = 0UZ; index < visibleRecords.size(); ++index) {
        const gr::log::LogRecord& record             = visibleRecords[index];
        const auto*               currentTable       = GImGui->CurrentTable;
        const float               messageColumnWidth = std::max(1.f, currentTable != nullptr && currentTable->ColumnsCount > 2 ? currentTable->Columns[2].WidthGiven - 2.f * ImGui::GetStyle().CellPadding.x : 1.f);
        const float               rowMinHeight       = std::max(ImGui::GetFrameHeight(), ImGui::CalcTextSize(record.text, record.text + record.textLength, false, messageColumnWidth).y + 2.f * ImGui::GetStyle().CellPadding.y);
        ImGui::TableNextRow(0, rowMinHeight);
        ImGui::TableNextColumn();
        ImGui::SetNextItemSelectionUserData(static_cast<ImGuiSelectionUserData>(index));
        ImGui::Selectable(std::format("{}###record{}", timeOfDay(record.timestampNanos), index).c_str(), _selectedRecords.Contains(selectionIdOf(record)), ImGuiSelectableFlags_SpanAllColumns, ImVec2(0.f, rowMinHeight));
        ImGui::TableNextColumn();
        ImGui::TextColored(colourOf(record.level), "%.*s", static_cast<int>(logLevelName(record.level).size()), logLevelName(record.level).data());
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
