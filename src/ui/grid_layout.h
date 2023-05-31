#pragma once
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

public:
    void ArrangePlots(std::span<Dashboard::Plot> plots, GridArrangement arrangement)noexcept {
        if (arrangement != m_arrangement) {
			m_arrangement = arrangement;
            switch (arrangement) {
            case DigitizerUi::GridArrangement::Horizontal:
                return RearrangePlotsHorizontal(plots);
            case DigitizerUi::GridArrangement::Vertical:
                break;
            case DigitizerUi::GridArrangement::Tiles:
                break;
            case DigitizerUi::GridArrangement::Free:
                break;
            default:
                break;
            }
		}
    }

private:
    static void RearrangePlotsHorizontal(std::span<Dashboard::Plot> plots) {

        size_t nplots = std::min(plots.size(), size_t(grid_width));
        uint32_t col_size = grid_width / nplots;
        uint32_t curr_col = 0;

        for (size_t i = 0; i < nplots - 1; i++, curr_col += col_size) {
            auto& plot = plots[i];
            plot.rect  = {
                .x = int(curr_col),
                .y = 0,
                .w = int(col_size),
                .h = int(grid_height),
            };
        }
        auto &plot = plots[nplots - 1];
        plot.rect  = {
             .x = int(curr_col),
             .y = 0,
             .w = int(grid_width - curr_col),
             .h = int(grid_height),
        };
	}

private:
    GridArrangement m_arrangement = GridArrangement::Free;
};

} // namespace DigitizerUi