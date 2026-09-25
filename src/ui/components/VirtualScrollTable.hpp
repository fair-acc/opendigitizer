#ifndef OPENDIGITIZER_UI_COMPONENTS_VIRTUAL_SCROLL_TABLE_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_VIRTUAL_SCROLL_TABLE_HPP_

#include <imgui.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace DigitizerUi::components {

/// Parameters for a table which only draws its visible rows, provided all the elements are the same height
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
/// ImGuiListClipper that draws the header based on strings. Use like so:
///
/// ```
/// VirtualScrollTable table({...});
/// while (auto visibleRange = table.step()) {
///     for (std::size_t i = visibleRange->first; i < visibleRange->second; ++i) {
///         table.beginRow();
///
///         // draw row here...
///     }
/// }
/// ```
struct VirtualScrollTable {

    /// the index of the header column that was clicked this frame, if any
    std::optional<std::size_t> clickedColumn;

    // calculated height of the header row after drawing
    float columnHeaderRowHeight = 0.f;

    /// If @p size is {0,0} then it will try to fill the rest of the available
    /// space while being at least a minimum the height of two elements
    explicit VirtualScrollTable(VirtualScrollTableParams tableParams, ImVec2 size = {});
    ~VirtualScrollTable();

    VirtualScrollTable(const VirtualScrollTable&)            = delete;
    VirtualScrollTable(VirtualScrollTable&&)                 = delete;
    VirtualScrollTable& operator=(const VirtualScrollTable&) = delete;
    VirtualScrollTable& operator=(VirtualScrollTable&&)      = delete;

    /// Returns [begin, end) indices of items that will be visible in this table. The caller must
    /// draw one row for each index in that range, calling beginRow() each time
    std::optional<std::pair<std::size_t, std::size_t>> step();

    /// Goes to the next virtual scroll table row, use ImGui::TableNextColumn() to draw each column
    void beginRow();

    /// ends the table early, normally happens in destructor
    void end();

private:
    VirtualScrollTableParams _params;
    ImGuiListClipper         _clipper      = {};
    bool                     _tableVisible = false;
};

} // namespace DigitizerUi::components

#endif
