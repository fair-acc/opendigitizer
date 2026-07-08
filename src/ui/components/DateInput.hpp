#ifndef OPENDIGITIZER_UI_COMPONENTS_DATE_INPUT_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_DATE_INPUT_HPP_

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

#include <ImGuiDatePicker.hpp>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <format>
#include <optional>
#include <string_view>

namespace DigitizerUi::components {

namespace detail {
constexpr const char* kExampleDate = "00/00/0000";
} // namespace detail

[[nodiscard]] inline bool drawCalendarButton() {
    constexpr const char* kIconCalendar = "\u{f073}";
    const ImVec2          squareButtonSize{ImGui::GetFrameHeight(), ImGui::GetFrameHeight()};

    ImGui::PushFont(LookAndFeel::instance().fontIcons, squareButtonSize.y / 2.f); // TODO: use FontWithSize when rebased on branch that added it
    const bool result = ImGui::Button(kIconCalendar, squareButtonSize);
    ImGui::PopFont();
    return result;
}

[[nodiscard]] inline std::optional<std::chrono::time_point<std::chrono::system_clock>> drawDateInput(const char* strId, std::chrono::time_point<std::chrono::system_clock> current) {
    constexpr const char* calendarPopupId = "Select date##calendarPopup";

    IMW::ChangeStrId id(strId);

    std::optional<std::chrono::time_point<std::chrono::system_clock>> newDate;

    constexpr std::size_t       dateStringMaxLength = 11;
    std::chrono::year_month_day date(std::chrono::floor<std::chrono::days>(current));
    char                        dateStr[dateStringMaxLength] = {};
    std::format_to_n(dateStr, dateStringMaxLength, "{:02}/{:02}/{:04}", static_cast<unsigned>(date.day()), static_cast<unsigned>(date.month()), static_cast<int>(date.year()));

    constexpr auto dateValidator = [](ImGuiInputTextCallbackData* d) -> int { // NOSONAR ('unecessarily' nonconst pointer)
        if (d->EventChar == '/' || (d->EventChar >= '0' && d->EventChar <= '9')) {
            return 0;
        }
        return 1;
    };

    ImGui::SetNextItemWidth(ImGui::CalcTextSize(detail::kExampleDate).x + ImGui::GetFrameHeight());
    if (ImGui::InputTextWithHint("##date", "today", dateStr, dateStringMaxLength, ImGuiInputTextFlags_CallbackCharFilter, dateValidator) && std::string_view(dateStr).size() == 10) {
        unsigned day = 0, month = 0;
        int      year = 0;
        std::from_chars(dateStr, dateStr + 2, day);
        std::from_chars(dateStr + 3, dateStr + 5, month);
        std::from_chars(dateStr + 6, dateStr + 10, year);
        std::chrono::year_month_day inputDate{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
        if (inputDate.ok()) {
            newDate = std::chrono::sys_days(inputDate);
        }
    }

    ImGui::SameLine();
    if (drawCalendarButton()) {
        ImGui::OpenPopup(calendarPopupId);
    }

    if (ImGui::IsPopupOpen(calendarPopupId)) {
        time_t  dateAsTimeT = std::chrono::system_clock::to_time_t(current);
        std::tm temporaryTime{};
        if (std::tm* localTime = localtime_r(&dateAsTimeT, &temporaryTime)) {
            if (ImGui::DatePicker(calendarPopupId, *localTime)) {
                newDate = std::chrono::system_clock::from_time_t(std::mktime(localTime));
            }
        } else {
            IMW::StyleColor redText(ImGuiCol_Text, LookAndFeel::instance().palette().errorColor);
            ImGui::TextUnformatted("Error parsing selected date...");
        }
    }

    return newDate;
}

[[nodiscard]] inline float calcDateInputWidth() { return ImGui::CalcTextSize(detail::kExampleDate).x + 2.f * ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x; }

} // namespace DigitizerUi::components

#endif
