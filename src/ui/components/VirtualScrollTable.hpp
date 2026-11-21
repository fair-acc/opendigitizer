#ifndef OPENDIGITIZER_UI_COMPONENTS_VIRTUAL_SCROLL_TABLE_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_VIRTUAL_SCROLL_TABLE_HPP_

#include "../common/LookAndFeel.hpp"
#include "StyledWidgets.hpp"

#include <imgui.h>
#include <imgui_internal.h> // for GetCurrentTable

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace gr::od::components {

struct VirtualScrollTableParams {
    struct SortMarker {
        std::size_t        columnIndex{};
        ImGuiSortDirection direction = ImGuiSortDirection_Ascending;
    };

    std::size_t                  numElements{};
    float                        elementHeight{};
    std::span<const std::string> columns;
    // 0: stretch
    std::span<const float>     fixedColumnWidths{};
    std::optional<std::size_t> scrollToElement{};
    ImFont*                    columnHeaderFont = nullptr;
    std::optional<SortMarker>  sortMarker{};
};

/// a table that draws only its visible rows, around ImGuiListClipper with a header of strings:
/// `while (auto rows = table.step()) { for (i in *rows) { table.beginRow(); ... } }`
struct VirtualScrollTable {
    /// the index of the header column that was clicked this frame, if any
    std::optional<std::size_t> clickedColumn;

    // calculated height of the header row after drawing
    float columnHeaderRowHeight = 0.f;

    VirtualScrollTableParams _params;
    ImGuiListClipper         _clipper      = {};
    bool                     _tableVisible = false;

    /// `size` {0,0}: the rest of the available space, at least two rows high
    explicit VirtualScrollTable(VirtualScrollTableParams tableParams, ImVec2 size = {}) : _params(std::move(tableParams)) {
        assert(!_params.columns.empty());
        assert(_params.elementHeight > 0.f);

        if (size.x == 0.f && size.y == 0.f) {
            const float minHeight = 2.f * _params.elementHeight + ImGui::GetFrameHeightWithSpacing();
            size                  = ImVec2{0.f, std::max(ImGui::GetContentRegionAvail().y, minHeight)};
        }

        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;
        _tableVisible                         = ImGui::BeginTable("##virtualScrollTable", static_cast<int>(_params.columns.size()), kTableFlags, size);
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
                const float    arrowScale = LookAndFeel::instance().sortArrowScale;
                const ImGuiDir arrowDir   = _params.sortMarker->direction == ImGuiSortDirection_Ascending ? ImGuiDir_Up : ImGuiDir_Down;
                const ImVec2   arrowPos   = ImGui::GetCursorScreenPos() + ImVec2{0.f, ImGui::GetStyle().FramePadding.y};
                ImGui::RenderArrow(ImGui::GetWindowDrawList(), arrowPos, ImGui::GetColorU32(ImGuiCol_Text), arrowDir, arrowScale);
                ImGui::Dummy(ImVec2{ImGui::GetFontSize(), 0.f});
                ImGui::SameLine();
            }
            if (selectable(column.c_str())) { // instead of ImGui::TableHeader
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

    /// draw each column after ImGui::TableNextColumn()
    void beginRow() {
        assert(_tableVisible && "only draw rows for the ranges returned by step()");
        ImGui::TableNextRow(ImGuiTableRowFlags_None, _params.elementHeight);
    }

    /// ends the table early, else the destructor does
    void end() {
        if (_tableVisible) {
            // moves the cursor down so the scrollbar accounts for the rows not drawn; clipper.Step() returning false calls it too, harmlessly
            _clipper.End();
            ImGui::EndTable();
        }
        _tableVisible = false;
    }
};

} // namespace gr::od::components

#endif
