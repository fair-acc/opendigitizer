#ifndef OPENDIGITIZER_DOCKING_H
#define OPENDIGITIZER_DOCKING_H

#include "../utils/ImGuiDockSpaceState.hpp"

#include <imgui.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

/// Place for Docking related components generic and agnostic to Opendigitizer code

namespace DigitizerUi {

enum class DockingLayoutType { Row, Column, Grid, Free };

inline constexpr const char* dockingLayoutName(DockingLayoutType type) {
    switch (type) {
    case DockingLayoutType::Row: return "Row";
    case DockingLayoutType::Column: return "Column";
    case DockingLayoutType::Grid: return "Grid";
    case DockingLayoutType::Free: return "Free";
    }

    return "unknown";
}

/// Hosts a group of dock windows
class DockSpace {
    gr::property_map  _lastFreeLayout;
    DockingLayoutType _layoutType      = DockingLayoutType::Free;
    bool              _needsRelayout   = true;
    bool              _lastIsEditable  = false;
    size_t            _lastWindowCount = 0;

public:
    struct Window;
    using Windows = std::vector<std::shared_ptr<Window>>;

    DockingLayoutType layoutType() const;
    bool              isFreeLayout() const;
    bool              isBoxLayout() const;

    void setLayoutType(DockingLayoutType);

    /// Renders the specified windows in an area of size paneSize
    /// May modify windows by setting Window::freeLayoutPosition to nullopt
    void render(const Windows& windows, ImVec2 paneSize, bool isEditable);

    // save and load the free layout (including any floating windows)
    const gr::property_map& saveFreeLayout() const { return _lastFreeLayout; }
    void                    loadFreeLayout(const gr::property_map& layout) { _lastFreeLayout = layout; }

private:
    static ImGuiID dockspaceID();

    void renderWindows(const Windows& windows, bool isEditable);
    void drawEditableWindowDragArea();

    // positions all windows according to the current layout type
    // this function does not restore the location of floating windows, renderWindows() will do that
    void relayout(const Windows& windows, bool isEditable, bool exactFreeLayoutRequested);

    // perform a box layout (aka row or column layout)
    void layoutInBox(const Windows& windows, ImGuiDir, bool isEditable);

    void layoutInGrid(const Windows& windows, bool isEditable);

    // returns false if layout in the exactly requested geometries fails
    [[nodiscard]] bool layoutInExactFree(const Windows& windows, bool isEditable);
    void               layoutInFreeRegion(const std::vector<std::vector<int>>& grid, const Windows& windows, std::size_t x0, std::size_t x1, std::size_t y0, std::size_t y1, ImGuiID nodeId, bool isEditable);

    // triggers a relayout for the next frame
    void setNeedsRelayout(bool);

    // returns the flags the node should use for the current layout type
    // these can be tweaked against UX requirements
    int nodeFlags(bool isEditable) const;
};

// make it a sub-class
struct DockSpace::Window {
    explicit Window(const std::string& n) : name(n) {}

    std::string           name;
    std::function<void()> renderFunc;
    std::function<void()> renderDockingContextMenuFunc;

    struct FreeGeometry {
        std::size_t x      = 0UL;
        std::size_t y      = 0UL;
        std::size_t width  = 0UL;
        std::size_t height = 0UL;
    };

    /// If set, this is applied on call to render(). Requires that windows with this set collectively cover all of paneSize
    /// Min point of the pane is (0, 0).
    /// This is only used at startup, loaded from "rect" field of plots in dashboard yaml
    std::optional<FreeGeometry> freeLayoutPosition;
};

} // namespace DigitizerUi

#endif
