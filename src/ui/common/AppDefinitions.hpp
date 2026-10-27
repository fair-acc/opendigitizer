#ifndef APP_DEFINITIONS_HPP_
#define APP_DEFINITIONS_HPP_

#include "AppLocation.hpp"

namespace DigitizerUi {
enum class ViewMode {
    VIEW,
    INTERACTION,
    LAYOUT,
    FLOWGRAPH,
    OPEN_SAVE_DASHBOARD,
};

using Page = Digitizer::AppLocation::Page;

/// VIEW, INTERACTION and LAYOUT are the same page (the dashboard) for the URL
[[nodiscard]] constexpr Page pageFor(ViewMode mode) noexcept {
    switch (mode) {
    case ViewMode::FLOWGRAPH: return Page::Flowgraph;
    case ViewMode::OPEN_SAVE_DASHBOARD: return Page::Dashboards;
    default: return Page::Chart;
    }
}

/// the editor and the open/save page are only reachable when editable; otherwise they fall back to the chart view
[[nodiscard]] constexpr ViewMode viewModeFor(Page page, ViewMode current, bool editable) noexcept {
    if (editable && page == Page::Flowgraph) {
        return ViewMode::FLOWGRAPH;
    }
    if (editable && page == Page::Dashboards) {
        return ViewMode::OPEN_SAVE_DASHBOARD;
    }
    if (pageFor(current) == Page::Chart) {
        return current;
    }
    return editable ? ViewMode::INTERACTION : ViewMode::VIEW;
}
} // namespace DigitizerUi

#endif
