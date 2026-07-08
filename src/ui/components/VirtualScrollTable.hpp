#ifndef OPENDIGITIZER_UI_COMPONENTS_VIRTUAL_SCROLL_TABLE_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_VIRTUAL_SCROLL_TABLE_HPP_

#include <imgui.h>
#include <imgui_internal.h> // for RenderArrow

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace DigitizerUi::components {

struct VirtualScrollTableParams {
    struct SortMarker {
        std::size_t        columnIndex{};
        ImGuiSortDirection direction = ImGuiSortDirection_Ascending;
    };

    std::size_t                  numElements{};
    float                        elementHeight{};
    std::span<const std::string> columns;
    // specify fixed withs for columns, 0 means stretch
    std::span<const float>     fixedColumnWidths{};
    std::optional<std::size_t> scrollToElement{};
    ImFont*                    columnHeaderFont = nullptr;
    std::optional<SortMarker>  sortMarker{};
};

/// A table which only draws its visible rows. Somewhat glorified wrapper around
/// ImGuiListClipper that draws the header based on strings. Usage:
///
/// VirtualScrollTable table({...});
/// while (auto visibleRange = table.step()) {
///     for (std::size_t i = visibleRange->first; i < visibleRange->second; ++i) {
///         table.beginRow();
///
///         // draw all columns of this row here...
///     }
/// }
struct VirtualScrollTable {
    /// the index of the header column that was clicked this frame, if any
    std::optional<std::size_t> clickedColumn;

    // calculated height of the header row after drawing
    float columnHeaderRowHeight = 0.f;

    /// @param size is the space to fill, if it is {0,0} then it will try to
    /// fill the rest of the available space while being at least a minimum the
    /// height of two elements
    explicit VirtualScrollTable(VirtualScrollTableParams tableParams, ImVec2 size = {}) : _params(std::move(tableParams)) {
        assert(!_params.columns.empty());
        assert(_params.elementHeight > 0.f);

        if (size.x == 0.f && size.y == 0.f) {
            // minimum size of two rows plus the columns headers
            const float minHeight = 2.f * _params.elementHeight + ImGui::GetFrameHeightWithSpacing();
            size                  = ImVec2{0.f, std::max(ImGui::GetContentRegionAvail().y, minHeight)};
        }

        constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;
        _tableVisible                        = ImGui::BeginTable("##virtualScrollTable", static_cast<int>(_params.columns.size()), tableFlags, size);
        if (!_tableVisible) {
            return;
        }

        for (std::size_t columnIndex = 0UZ; columnIndex < _params.columns.size(); ++columnIndex) {
            const float fixedWidth = columnIndex < _params.fixedColumnWidths.size() ? _params.fixedColumnWidths[columnIndex] : 0.f;
            ImGui::TableSetupColumn(_params.columns[columnIndex].c_str(), fixedWidth > 0.f ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_None, fixedWidth);
        }
        ImGui::TableSetupScrollFreeze(0, 1);

        if (_params.columnHeaderFont != nullptr) {
            ImGui::PushFont(_params.columnHeaderFont);
        }
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (std::size_t columnIndex = 0UZ; columnIndex < _params.columns.size(); ++columnIndex) {
            const std::string& column = _params.columns[columnIndex];
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (column.empty()) {
                continue;
            }
            if (_params.sortMarker && _params.sortMarker->columnIndex == columnIndex) {
                constexpr float arrowScale = 0.75f;
                const ImGuiDir  arrowDir   = _params.sortMarker->direction == ImGuiSortDirection_Ascending ? ImGuiDir_Up : ImGuiDir_Down;
                const ImVec2    arrowPos   = ImGui::GetCursorScreenPos() + ImVec2{0.f, ImGui::GetStyle().FramePadding.y};
                ImGui::RenderArrow(ImGui::GetWindowDrawList(), arrowPos, ImGui::GetColorU32(ImGuiCol_Text), arrowDir, arrowScale);
                ImGui::Dummy(ImVec2{ImGui::GetFontSize(), 0.f});
                ImGui::SameLine();
            }
            if (ImGui::Selectable(column.c_str())) { // instead of ImGui::TableHeader
                clickedColumn = columnIndex;
            }
        }
        if (_params.columnHeaderFont != nullptr) {
            ImGui::PopFont();
        }

        columnHeaderRowHeight = ImGui::GetCurrentTable()->RowPosY2 - ImGui::GetCurrentTable()->RowPosY1;

        if (_params.scrollToElement.has_value()) {
            // BeginTable() with ScrollY made the table's inner child the current window
            const float elementTop = _params.elementHeight * static_cast<float>(*_params.scrollToElement);
            ImGui::SetScrollY(std::max(0.f, elementTop - (ImGui::GetWindowHeight() - _params.elementHeight) * 0.5f));
        }

        _clipper.Begin(static_cast<int>(_params.numElements), _params.elementHeight);
    }

    ~VirtualScrollTable() { end(); }

    VirtualScrollTable(const VirtualScrollTable&)            = delete;
    VirtualScrollTable(VirtualScrollTable&&)                 = delete;
    VirtualScrollTable& operator=(const VirtualScrollTable&) = delete;
    VirtualScrollTable& operator=(VirtualScrollTable&&)      = delete;

    std::optional<std::pair<std::size_t, std::size_t>> step() {
        if (_clipper.Step()) {
            return std::make_pair(static_cast<std::size_t>(_clipper.DisplayStart), static_cast<std::size_t>(_clipper.DisplayEnd));
        }
        return {};
    }

    /// Goes to the next virtual scroll table row, use ImGui::TableNextColumn() to draw each column
    void beginRow() {
        assert(_tableVisible && "only draw rows for the ranges returned by step()");
        ImGui::TableNextRow(ImGuiTableRowFlags_None, _params.elementHeight);
    }

    /// ends the table early, normally happens in destructor
    void end() {
        if (_tableVisible) {
            // move the cursor far down so that scrollbar looks like it is accounting for the rows we are not rendering.
            // automatically called when clipper.Step() returns false but no harm in calling it twice
            _clipper.End();
            ImGui::EndTable();
        }
        _tableVisible = false;
    }

    VirtualScrollTableParams _params;
    ImGuiListClipper         _clipper      = {};
    bool                     _tableVisible = false;
};

} // namespace DigitizerUi::components

#endif
