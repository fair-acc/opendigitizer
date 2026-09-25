#include "VirtualScrollTable.hpp"

#include <imgui_internal.h> // for RenderArrow

#include <algorithm>
#include <cassert>

namespace DigitizerUi::components {

VirtualScrollTable::VirtualScrollTable(VirtualScrollTableParams tableParams, ImVec2 size) : _params(std::move(tableParams)) {
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

VirtualScrollTable::~VirtualScrollTable() { end(); }

std::optional<std::pair<std::size_t, std::size_t>> VirtualScrollTable::step() {
    if (_clipper.Step()) {
        return std::make_pair(static_cast<std::size_t>(_clipper.DisplayStart), static_cast<std::size_t>(_clipper.DisplayEnd));
    }
    return {};
}

void VirtualScrollTable::beginRow() {
    assert(_tableVisible && "only draw rows for the ranges returned by step()");
    ImGui::TableNextRow(ImGuiTableRowFlags_None, _params.elementHeight);
}

void VirtualScrollTable::end() {
    if (_tableVisible) {
        // move the cursor far down so that scrollbar looks like it is accounting for the rows we are not rendering.
        // automatically called when clipper.Step() returns false but no harm in calling it twice
        _clipper.End();
        ImGui::EndTable();
    }
    _tableVisible = false;
}

} // namespace DigitizerUi::components
