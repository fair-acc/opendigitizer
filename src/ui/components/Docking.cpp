#include "Docking.hpp"
#include "../ui/common/ImguiWrap.hpp"
#include "../ui/components/ImGuiNotify.hpp"

#include <algorithm>
#include <format>
#include <ranges>

#include <imgui_internal.h>

using namespace DigitizerUi;

constexpr const char* kDockspaceId  = "OpendigitizerDockspace";
constexpr const char* kHostWindowId = "MainDockspace_Window";

DockingLayoutType DockSpace::layoutType() const { return _layoutType; }

ImGuiID DockSpace::dockspaceID() { return ImGui::GetID(kDockspaceId); }

static void setFlagsForAllDockNodes(ImGuiDockNodeFlags flags) {
    ImGuiContext& g = *GImGui;
    for (int n = 0; n < g.DockContext.Nodes.Data.Size; n++) {
        if (ImGuiDockNode* node = static_cast<ImGuiDockNode*>(g.DockContext.Nodes.Data[n].val_p)) {
            if (!node->IsDockSpace()) {
                node->SetLocalFlags(flags);
            }
        }
    }
}

static void dragWindow(ImGuiWindow* windowToDrag, ImRect boundingBox) {
    ImGuiContext& g = *GImGui;
    // found in the internals of ImGui::TabItemEx()
    ImGui::DockContextQueueUndockWindow(&g, windowToDrag);

    // copy of ImGui::StartMouseMovingWindow(), with two changes:
    // - the ActiveIdClickOffset calculation is from ImGui::TabItemEx(), which
    //   expects an initially-docked window.
    // - the part of StartMouseMovingWindow() which checks for ancestor windows
    //   with the NoMove flag, and cancels the movement if so, is removed (our
    //   main window is NoMove)
    // - click position is clamped to be in the window frame/header
    ImGui::FocusWindow(windowToDrag);
    ImGui::SetActiveID(windowToDrag->MoveId, windowToDrag);
    if (g.IO.ConfigNavCursorVisibleAuto) {
        g.NavCursorVisible = false;
    }
    g.ActiveIdClickOffset -= windowToDrag->Pos - boundingBox.Min;
    g.ActiveIdNoClearOnFocusLoss = true;
    ImGui::SetActiveIdUsingAllKeyboardKeys();
    g.MovingWindow = windowToDrag;

    // set the click offset to within the window frame, currently ImGui::BeginDockableDragDropSource() will only
    // start the docking drag + drop process if the click is specifically between the top of the window and
    // the window frame height, where the tab bar is (see is_drag_docking in the aforementioned function).
    g.ActiveIdClickOffset.y = std::clamp(g.ActiveIdClickOffset.y, 0.f, std::max(0.f, ImGui::GetFrameHeight() - 1.f));
};

void DockSpace::setLayoutType(DockingLayoutType type) {
    if (type != _layoutType) {
        _layoutType = type;
        setNeedsRelayout(true);
        if (type == DockingLayoutType::Free) {
            // isEditable passed to nodeFlags is always true- because layout type can only change in the editable mode
            setFlagsForAllDockNodes(nodeFlags(true));
        }
    }
}

void DockSpace::clearWindowGeometry(const Windows& windows) {
    for (auto w : windows) {
        w->clearGeometry();
    }
}

void DockSpace::render(const Windows& windows, ImVec2 paneSize, bool isEditable) {

    {
        ImGui::SetNextWindowSize(paneSize);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        IMW::Child child(kHostWindowId, paneSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoMove);

        ImGui::PopStyleVar();

        setNeedsRelayout(_needsRelayout || _lastWindowCount != windows.size() || !ImGui::DockBuilderGetNode(dockspaceID()));
        _lastWindowCount = windows.size();

        if (_needsRelayout) {
            relayout(windows, isEditable);
        } else if (_lastIsEditable != isEditable) {
            setFlagsForAllDockNodes(nodeFlags(isEditable));
            _lastIsEditable = isEditable;
        }

        ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
        ImGui::DockSpace(dockspaceID(), ImVec2(0.0f, 0.0f), dockspace_flags, nullptr);
    }

    renderWindows(windows, isEditable);
}

void DockSpace::renderWindows(const Windows& windows, bool isEditable) {
    for (const auto& window : windows) {
        constexpr float floatingWindowMinSizeFractionOfMainWindow = 1.f / 4.f;
        const ImVec2    windowSizeMax                             = ImGui::GetMainViewport()->WorkSize;
        const ImVec2    windowSizeMin                             = {
            std::max(1.f, windowSizeMax.x * floatingWindowMinSizeFractionOfMainWindow),
            std::max(1.f, windowSizeMax.y * floatingWindowMinSizeFractionOfMainWindow),
        };
        ImGui::SetNextWindowSizeConstraints(windowSizeMin, windowSizeMax);

        IMW::Window dock(window->name.data(), nullptr, ImGuiWindowFlags_NoCollapse);

        if (window->renderFunc) {
            window->renderFunc();
        }

        if (isEditable) {
            drawEditableWindowDragArea();
        }

        // TODO: ImGui bug, it seems local flags on a window are reset after docking.
        // use the no tab bar flag as a marker to see if our flags are still there.
        // nodeFlags() also specifies ImGuiDockNodeFlags_NoWindowMenuButton for a similar
        // reason
        if (auto node = ImGui::GetWindowDockNode(); node && !node->IsNoTabBar()) {
            node->SetLocalFlags(nodeFlags(isEditable));
        }

        // Write back geometry info, as user might have used the splitters
        if (isBoxLayout()) {
            // window->setGeometry(ImGui::GetWindowPos(), ImGui::GetWindowSize());
        } else if (layoutType() == DockingLayoutType::Free) {
            if (auto node = ImGui::GetWindowDockNode()) {
                node->SetLocalFlags(node->LocalFlags | ImGuiDockNodeFlags_NoDockingOverMe);
            }
        }
    }
}

void DockSpace::drawEditableWindowDragArea() {
    auto* const currentWindow = ImGui::GetCurrentWindow();
    const auto  dragArea      = currentWindow->ContentRegionRect;

    if (dragArea.GetArea() <= 0.f) {
        components::Notification::warning("Unable to create drag and drop button for zero-sized window");
        return;
    }

    const auto oldPos = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(dragArea.GetTL());
    auto buttonFlags = ImGuiButtonFlags_PressedOnClickRelease | ImGuiButtonFlags_AllowOverlap;
    if (GImGui->DragDropActive && !GImGui->DragDropPayload.IsDataType(IMGUI_PAYLOAD_TYPE_WINDOW)) {
        buttonFlags |= ImGuiButtonFlags_PressedOnDragDropHold;
    }
    const bool pressed = ImGui::InvisibleButton("windowDragButton", dragArea.GetSize(), buttonFlags);
    const bool held    = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();

    const bool windowAlreadyBeingDragged = GImGui->MovingWindow == currentWindow;
    const bool isFloating                = !currentWindow->DockIsActive;

    if (hovered || pressed || held) {
        const uint32_t highlightColor = [&] {
            if (windowAlreadyBeingDragged) {
                return 0x33333333U; // window is being dragged
            } else if ((held || pressed) && !isFloating) {
                return 0x3300FF00U; // almost dragging a docked window, but drag lock is in effect
            } else {
                return 0x33FF0000U; // just hovering
            }
        }();
        currentWindow->DrawList->AddRectFilled(dragArea.GetTR(), dragArea.GetBL(), highlightColor);
    }

    if (!windowAlreadyBeingDragged && held) {
        const float dragMinimumDelta = isFloating ? 0.0f : 30.f;

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, dragMinimumDelta)) {
            dragWindow(currentWindow, dragArea);
        }
    }

    ImGui::SetCursorScreenPos(oldPos);
}

void DockSpace::layoutInBox(const Windows& windows, ImGuiDir direction, bool isEditable) {
    auto initNode = [&](ImGuiID nodeId, const std::shared_ptr<Window>& window) {
        auto node = ImGui::DockBuilderGetNode(nodeId);
        node->SetLocalFlags(nodeFlags(isEditable));
        ImGui::DockBuilderDockWindow(window->name.data(), nodeId);
    };

    ImGuiID rightId     = dockspaceID();
    size_t  windowCount = windows.size();
    for (auto window : windows) {
        float ratio = 1.0f / float(windowCount);
        if (windowCount > 1) {
            ImGuiID leftId;
            ImGui::DockBuilderSplitNode(rightId, direction, ratio, &leftId, &rightId);
            initNode(leftId, window);
        } else {
            // last window gets all the space that's left
            initNode(rightId, window);
        }
        windowCount--;
    }
}

void DockSpace::layoutInGrid(const Windows& windows, bool isEditable) {
    const size_t windowCount = windows.size();
    const int    columns     = int(std::ceil(std::sqrt(windowCount)));
    const int    rows        = int(std::ceil(double(windowCount) / static_cast<double>(columns)));

    ImGuiID bottomId  = dockspaceID();
    size_t  windowIdx = 0;

    for (int r = 0; r < rows; r++) {
        float   ratio     = 1.0f / float(rows - r);
        ImGuiID rowDockId = bottomId;

        if (r + 1 < rows) {
            // if its not the last row, we need to split
            ImGuiID topId;
            ImGui::DockBuilderSplitNode(bottomId, ImGuiDir_Up, ratio, &topId, &bottomId);
            rowDockId = topId;
        }

        for (int c = 0; c < columns && windowIdx < windowCount; c++, windowIdx++) {
            float colRatio = 1.0f / float(columns - c);

            if (windowIdx + 1 == windowCount) {
                // last window, occupy whatever is left
                const auto& window = windows[windowIdx];
                ImGui::DockBuilderDockWindow(window->name.data(), rowDockId);

                auto node = ImGui::DockBuilderGetNode(rowDockId);
                node->SetLocalFlags(nodeFlags(isEditable));
                break;
            }

            ImGuiID leftId;
            ImGui::DockBuilderSplitNode(rowDockId, ImGuiDir_Left, colRatio, &leftId, &rowDockId);
            auto node = ImGui::DockBuilderGetNode(leftId);
            node->SetLocalFlags(nodeFlags(isEditable));

            const auto& window = windows[windowIdx];
            ImGui::DockBuilderDockWindow(window->name.data(), leftId);
        }
    }
}

void DockSpace::layoutInFree(const Windows& windows, bool isEditable) {
    if (windows.empty()) {
        return;
    }

    // Assume that minX = 0, minY = 0
    std::size_t maxX = std::ranges::max(windows | std::views::transform([](auto const& w) { return w->x + w->width; }));
    std::size_t maxY = std::ranges::max(windows | std::views::transform([](auto const& w) { return w->y + w->height; }));
    if (maxX == 0 || maxY == 0) {
        return;
    }

    std::vector<std::vector<int>> grid(maxX, std::vector<int>(maxY, -1));

    // No overlap -> each cell belongs to exactly one window
    bool isOverlapDetected = false;
    for (std::size_t i = 0; i < windows.size(); i++) {
        const auto& w = windows[i];
        for (std::size_t x = w->x; x < w->x + w->width; x++) {
            for (std::size_t y = w->y; y < w->y + w->height; y++) {
                if (grid[x][y] != -1) {
                    isOverlapDetected = true;
                }
                grid[x][y] = static_cast<int>(i);
            }
        }
    }

    const bool isEmptyCellDetected = std::ranges::any_of(grid | std::views::join, [](int cellValue) { return cellValue == -1; });

    // if layout is ill-formed -> inform user and use grid layout
    if (isOverlapDetected) {
        components::Notification::error("Free layout is ill-formed, overlapped cells detected.");
        layoutInGrid(windows, isEditable);
    } else if (isEmptyCellDetected) {
        components::Notification::error("Free layout is ill-formed, empty cells detected.");
        layoutInGrid(windows, isEditable);
    } else {
        layoutInFreeRegion(grid, windows, 0, maxX, 0, maxY, dockspaceID(), isEditable);
    }
}

void DockSpace::layoutInFreeRegion(const std::vector<std::vector<int>>& grid, const Windows& windows, std::size_t x0, std::size_t x1, std::size_t y0, std::size_t y1, ImGuiID nodeId, bool isEditable) {
    // Check if entire region belongs to exactly one window ID
    assert(grid[x0][y0] >= 0);
    const std::size_t firstId = static_cast<std::size_t>(grid[x0][y0]);
    const bool        allSame = std::ranges::all_of( // x in [x0, x1)
        std::views::iota(x0, x1), [&](std::size_t x) {
            return std::ranges::all_of( // y in [y0, y1)
                std::views::iota(y0, y1), [&](std::size_t y) { return static_cast<std::size_t>(grid[x][y]) == firstId; });
        });

    if (allSame) {
        // last window, occupy whatever is left
        if (firstId < windows.size()) {
            ImGui::DockBuilderDockWindow(windows[firstId]->name.c_str(), nodeId);
        } else {
            ImGui::DockBuilderDockWindow(std::to_string(firstId).c_str(), nodeId); // empty draw
        }
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(nodeId);
        node->SetLocalFlags(nodeFlags(isEditable));
        return;
    }

    // Try vertical split: left/right
    for (std::size_t cutX = x0 + 1; cutX < x1; cutX++) {
        const bool canSplit = std::ranges::all_of(std::views::iota(y0, y1), [&](auto y) { return grid[cutX][y] != grid[cutX - 1][y]; });
        if (canSplit) {
            float   fraction = float(cutX - x0) / float(x1 - x0);
            ImGuiID leftNode, rightNode;
            ImGui::DockBuilderSplitNode(nodeId, ImGuiDir_Left, fraction, &leftNode, &rightNode);
            layoutInFreeRegion(grid, windows, x0, cutX, y0, y1, leftNode, isEditable);
            layoutInFreeRegion(grid, windows, cutX, x1, y0, y1, rightNode, isEditable);
            return;
        }
    }

    // Try horizontal split: top/bottom
    for (std::size_t cutY = y0 + 1; cutY < y1; cutY++) {
        const bool canSplit = std::ranges::all_of(std::views::iota(x0, x1), [&](auto x) { return grid[x][cutY] != grid[x][cutY - 1]; });
        if (canSplit) {
            float   fraction = float(cutY - y0) / float(y1 - y0);
            ImGuiID topNode, bottomNode;
            ImGui::DockBuilderSplitNode(nodeId, ImGuiDir_Up, fraction, &topNode, &bottomNode);
            layoutInFreeRegion(grid, windows, x0, x1, y0, cutY, topNode, isEditable);
            layoutInFreeRegion(grid, windows, x0, x1, cutY, y1, bottomNode, isEditable);
            return;
        }
    }
}

void DockSpace::relayout(const Windows& windows, bool isEditable) {
    const ImGuiID dockspaceID = this->dockspaceID();
    ImGui::DockBuilderAddNode(dockspaceID);
    ImGui::DockBuilderSetNodeSize(dockspaceID, ImGui::GetWindowSize());

    if (isBoxLayout()) {
        layoutInBox(windows, _layoutType == DockingLayoutType::Row ? ImGuiDir_Left : ImGuiDir_Up, isEditable);
    } else if (isFreeLayout()) {
        layoutInFree(windows, isEditable);
    } else {
        layoutInGrid(windows, isEditable);
    }

    ImGui::DockBuilderFinish(dockspaceID);

    _needsRelayout = false;
}

void DockSpace::setNeedsRelayout(bool needs) { _needsRelayout = needs; }

bool DockSpace::isFreeLayout() const { return _layoutType == DockingLayoutType::Free; }
bool DockSpace::isBoxLayout() const { return _layoutType == DockingLayoutType::Row || _layoutType == DockingLayoutType::Column; }

int DockSpace::nodeFlags(bool isEditable) const {
    int flags = ImGuiDockNodeFlags_None;

    if (isFreeLayout()) {
        // TODO: ImGui bug: When detached and redocked, the window menu button comes back
        flags |= ImGuiDockNodeFlags_NoWindowMenuButton;
    }

    if (!isEditable) {
        flags |= ImGuiDockNodeFlags_NoUndocking;
        flags |= ImGuiDockNodeFlags_NoResize;
    }

    flags |= ImGuiDockNodeFlags_NoTabBar;

    return flags;
}
