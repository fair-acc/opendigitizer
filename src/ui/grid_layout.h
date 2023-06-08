#ifndef GRID_LAYOUT_H
#define GRID_LAYOUT_H
#include "dashboard.h"

namespace DigitizerUi {

enum class GridArrangement {
    Horizontal,
    Vertical,
    Tiles,
    Free,
};

class GridLayout {
public:
    constexpr static inline auto grid_width  = 16u;
    constexpr static inline auto grid_height = 16u;
    constexpr static inline auto max_plots   = 16u;
    struct dirty_guard {
        bool &flag;
        dirty_guard(bool &flag) noexcept
            : flag(flag) {}
        ~dirty_guard() { flag = false; }
    };

public:
    void ArrangePlots(std::span<Dashboard::Plot> plots) noexcept {
        dirty_guard guard{ dirty };
        if (!plots.size()) return;

        if (!dirty)
            return MaintainLayout(plots);

        switch (m_arrangement) {
        case DigitizerUi::GridArrangement::Horizontal:
            return RearrangePlotsHorizontal(plots);
        case DigitizerUi::GridArrangement::Vertical:
            return RearrangePlotsVertical(plots);
        case DigitizerUi::GridArrangement::Tiles:
            return RearrangePlotsTiles(plots);
        case DigitizerUi::GridArrangement::Free:
            break;
        default:
            break;
        }
    }
    void SetArrangement(GridArrangement arrangement) noexcept {
        dirty         = true;
        m_arrangement = arrangement;
    }
    auto Arrangement() const noexcept { return m_arrangement; }
    void Invalidate() noexcept { dirty = true; }

private:
    void MaintainLayout(std::span<Dashboard::Plot> plots) noexcept {
        switch (m_arrangement) {
        case DigitizerUi::GridArrangement::Horizontal: {
            int32_t snap = grid_width;
            for (auto plot_it = plots.rbegin(); plot_it != plots.rend(); plot_it++) {
                plot_it->rect = {
                    .x = (plot_it->rect.x >= 0) * plot_it->rect.x,
                    .y = 0,
                    .w = std::max(snap - plot_it->rect.x, 1),
                    .h = grid_height,
                };
                snap = plot_it->rect.x;
            }
            break;
        }
        case DigitizerUi::GridArrangement::Vertical: {
            uint32_t snap = grid_width;
            for (auto it = plots.rbegin(); it != plots.rend(); it++) {
                auto &plot  = *it;
                plot.rect.w = grid_height;
                plot.rect.x = 0;
                plot.rect.h = snap - plot.rect.y;
                snap        = (plot.rect.y >= 0) * plot.rect.y;
            }
            break;
        }
        case DigitizerUi::GridArrangement::Tiles: {
            RearrangePlotsTiles(plots);
            break;
        }
        case DigitizerUi::GridArrangement::Free:
            break;
        default:
            break;
        }
    }
    static void RearrangePlotsHorizontal(std::span<Dashboard::Plot> plots) noexcept {
        size_t nplots = plots.size();

        //[[assume(grid_width >= nplots)]]
        uint32_t col_size = grid_width / nplots;
        uint32_t curr_col = 0;

        for (size_t i = 0; i < nplots - 1; i++, curr_col += col_size) {
            plots[i].rect = {
                .x = int(curr_col),
                .y = 0,
                .w = int(col_size),
                .h = int(grid_height),
            };
        }
        plots[nplots - 1].rect = {
            .x = int(curr_col),
            .y = 0,
            .w = int(grid_width - curr_col),
            .h = int(grid_height),
        };
    }
    static void RearrangePlotsVertical(std::span<Dashboard::Plot> plots) noexcept {
        size_t nplots = plots.size();

        //[[assume(grid_height >= nplots)]]
        uint32_t col_size = grid_height / nplots;
        uint32_t curr_col = 0;

        for (size_t i = 0; i < nplots - 1; i++, curr_col += col_size) {
            plots[i].rect = {
                .x = 0,
                .y = int(curr_col),
                .w = int(grid_width),
                .h = int(col_size),
            };
        }
        plots[nplots - 1].rect = {
            .x = 0,
            .y = int(curr_col),
            .w = int(grid_width),
            .h = int(grid_height - curr_col),
        };
    }
    static void RearrangePlotsTiles(std::span<Dashboard::Plot> plots) noexcept {
        size_t nplots  = plots.size();
        size_t columns = std::ceil(std::sqrt(nplots));
        size_t rows    = std::ceil(nplots / static_cast<double>(columns));

        size_t deltax
                = grid_width / columns;
        size_t deltay = grid_height / rows;

        size_t curr_n = 0;
        size_t curr_w = 0;
        size_t curr_h = 0;

        for (size_t i = 0; i < rows; i++) {
            for (size_t j = 0; j < columns && curr_n < nplots; j++, curr_n++) {
                plots[i * columns + j].rect = {
                    .x = int(curr_w),
                    .y = int(curr_h),
                    .w = int(curr_n == nplots - 1 || j == columns - 1 ? grid_width - curr_w : deltax), // if last plot in row, fill the rest of the width
                    .h = int(i == rows - 1 ? grid_height - curr_h : deltay),                           // if last row, fill the rest of the height
                };
                curr_w += deltax;
            }
            curr_w = 0;
            curr_h += deltay;
        }
    }

private:
    GridArrangement m_arrangement = GridArrangement::Tiles;
    bool            dirty         = true;
};
} // namespace DigitizerUi

#endif // !GRID_LAYOUT_H