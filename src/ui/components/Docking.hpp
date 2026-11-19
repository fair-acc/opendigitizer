#ifndef OPENDIGITIZER_DOCKING_H
#define OPENDIGITIZER_DOCKING_H

#include "../utils/ImGuiDockSpaceState.hpp"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gr::od {

enum class DockingLayoutType { Row, Column, Grid, Free };

struct LayoutRect { // fractions of the layout area, in [0, 1]
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
};

[[nodiscard]] std::vector<LayoutRect> autoLayoutRects(DockingLayoutType type, std::size_t windowCount);

[[nodiscard]] std::vector<LayoutRect> freeLayoutRects(std::span<const std::array<std::int64_t, 4>> cells); // negative extents count as 0

struct DockSpace {
    struct Window;
    using Windows = std::vector<std::shared_ptr<Window>>;

    mutable gr::property_map _lastFreeLayout;
    std::vector<std::string> _lastWindowNames;
    ImGuiID                  _lastDockspaceID        = 0;
    DockingLayoutType        _layoutType             = DockingLayoutType::Free;
    bool                     _needsRelayout          = true;
    bool                     _lastIsEditable         = false;
    bool                     _exactFreeLayoutApplied = false;

    DockingLayoutType layoutType() const;
    bool              isFreeLayout() const;
    bool              isBoxLayout() const;

    void setLayoutType(DockingLayoutType);

    void render(const Windows& windows, ImVec2 paneSize, bool isEditable);

    const gr::property_map& saveFreeLayout() const;
    void                    loadFreeLayout(const gr::property_map& layout) {
        _lastFreeLayout         = layout;
        _exactFreeLayoutApplied = true;
    }

    static ImGuiID dockspaceID();

    void captureFreeLayout() const;

    void renderWindows(const Windows& windows, bool isEditable, ImVec2 paneSize);
    void drawEditableWindowDragArea();

    // this function does not restore the location of floating windows, renderWindows() will do that
    void relayout(const Windows& windows, bool isEditable, bool exactFreeLayoutRequested);

    // a row or a column
    void layoutInBox(const Windows& windows, ImGuiDir, bool isEditable);

    void layoutInGrid(const Windows& windows, bool isEditable);

    // returns false if layout in the exactly requested geometries fails
    [[nodiscard]] bool layoutInExactFree(const Windows& windows, bool isEditable);
    void               layoutInFreeRegion(const std::vector<std::vector<int>>& grid, const Windows& windows, std::size_t x0, std::size_t x1, std::size_t y0, std::size_t y1, ImGuiID nodeId, bool isEditable);

    // triggers a relayout for the next frame
    void setNeedsRelayout(bool);

    int nodeFlags(bool isEditable) const;
};

struct DockSpace::Window {
    struct FreeGeometry {
        std::size_t x      = 0UL;
        std::size_t y      = 0UL;
        std::size_t width  = 0UL;
        std::size_t height = 0UL;
    };

    std::string           name;
    std::function<void()> renderFunc;
    std::function<void()> renderDockingContextMenuFunc;

    /// applied by render() when set; the windows with it set must together cover paneSize, whose min point is (0, 0);
    /// only at startup, from the "rect" field of the dashboard yaml's plots
    std::optional<FreeGeometry> freeLayoutPosition;

    bool wantsHorizontalScrollbar = false;

    explicit Window(const std::string& n) : name(n) {}
};

} // namespace gr::od

#endif
