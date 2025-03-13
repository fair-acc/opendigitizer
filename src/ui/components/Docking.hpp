#ifndef OPENDIGITIZER_DOCKING_H
#define OPENDIGITIZER_DOCKING_H

#include <imgui.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

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
    DockingLayoutType _layoutType      = DockingLayoutType::Free;
    bool              _needsRelayout   = true;
    size_t            _lastWindowCount = 0;

public:
    struct Window;
    using Windows = std::vector<std::shared_ptr<Window>>;

    DockingLayoutType layoutType() const;
    bool              isFreeLayout() const;
    bool              isBoxLayout() const;

    void setLayoutType(DockingLayoutType);

    /// Renders the specified windows in an area of size paneSize
    void render(const Windows& windows, ImVec2 paneSize);

private:
    static ImGuiID dockspaceID();

    void renderWindows(const Windows& windows);

    void clearWindowGeometry(const Windows& windows);

    // positions all windows according to the current layout type
    void relayout(const Windows& windows);

    // perform a box layout (aka row or column layout)
    void layoutInBox(const Windows& windows, ImGuiDir);

    void layoutInGrid(const Windows& windows);

    void layoutInFree(const Windows& windows);
    void layoutInFreeRegion(const std::vector<std::vector<int>>& grid, const Windows& windows, std::size_t x0, std::size_t x1, std::size_t y0, std::size_t y1, ImGuiID nodeId);

    // triggers a relayout for the next frame
    void setNeedsRelayout(bool);

    // returns the flags the node should use for the current layout type
    // these can be tweaked against UX requirements
    int nodeFlags() const;
};

// make it a sub-class
struct DockSpace::Window {
    explicit Window(const std::string& n) : name(n) {}

    std::string           name;
    int                   x      = 0;
    int                   y      = 0;
    int                   width  = 0;
    int                   height = 0;
    std::function<void()> renderFunc;

    void clearGeometry() { setGeometry({}, {}); }

    void setGeometry(ImVec2 pos, ImVec2 size) {
        x      = static_cast<int>(pos.x);
        y      = static_cast<int>(pos.y);
        width  = static_cast<int>(size.x);
        height = static_cast<int>(size.y);
    }

    [[nodiscard]] bool hasSize() const { return width > 0 && height > 0; }
};

} // namespace DigitizerUi

#endif
