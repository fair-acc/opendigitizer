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

void DockSpace::setLayoutType(DockingLayoutType type) {
    if (type != _layoutType) {
        _layoutType = type;
        setNeedsRelayout(true);
        if (type == DockingLayoutType::Free) {
            setFlagsForAllDockNodes(nodeFlags());
        }
    }
}

void DockSpace::clearWindowGeometry(const Windows& windows) {
    for (auto w : windows) {
        w->clearGeometry();
    }
}

void DockSpace::render(const Windows& windows, ImVec2 paneSize) {

    {
        ImGui::SetNextWindowSize(paneSize);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        IMW::Child child(kHostWindowId, paneSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoMove);

        ImGui::PopStyleVar();

        setNeedsRelayout(_needsRelayout || _lastWindowCount != windows.size() || !ImGui::DockBuilderGetNode(dockspaceID()));
        _lastWindowCount = windows.size();

        if (_needsRelayout) {
            relayout(windows);
        }

        ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
        ImGui::DockSpace(dockspaceID(), ImVec2(0.0f, 0.0f), dockspace_flags, nullptr);
    }

    renderWindows(windows);
}

void DockSpace::renderWindows(const Windows& windows) {
    for (const auto& window : windows) {
        IMW::Window dock(window->name.data(), nullptr, ImGuiWindowFlags_NoCollapse);

        if (window->renderFunc) {
            window->renderFunc();
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

void DockSpace::layoutInBox(const Windows& windows, ImGuiDir direction) {
    auto initNode = [this](ImGuiID nodeId, const std::shared_ptr<Window>& window) {
        auto node = ImGui::DockBuilderGetNode(nodeId);
        node->SetLocalFlags(nodeFlags());
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

void DockSpace::layoutInGrid(const Windows& windows) {
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
                node->SetLocalFlags(nodeFlags());
                break;
            }

            ImGuiID leftId;
            ImGui::DockBuilderSplitNode(rowDockId, ImGuiDir_Left, colRatio, &leftId, &rowDockId);
            auto node = ImGui::DockBuilderGetNode(leftId);
            node->SetLocalFlags(nodeFlags());

            const auto& window = windows[windowIdx];
            ImGui::DockBuilderDockWindow(window->name.data(), leftId);
        }
    }
}

void DockSpace::layoutInFree(const Windows& windows) {
    if (windows.empty()) {
        return;
    }

    // Assume that minX = 0, minY = 0
    int maxX = std::ranges::max(windows | std::views::transform([](auto const& w) { return w->x + w->width; }));
    int maxY = std::ranges::max(windows | std::views::transform([](auto const& w) { return w->y + w->height; }));
    if (maxX <= 0 || maxY <= 0) {
        return;
    }

    std::vector<std::vector<int>> grid(static_cast<std::size_t>(maxX), std::vector<int>(static_cast<std::size_t>(maxY), -1));

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
        layoutInGrid(windows);
    } else if (isEmptyCellDetected) {
        components::Notification::error("Free layout is ill-formed, empty cells detected.");
        layoutInGrid(windows);
    } else {
        layoutInFreeRegion(grid, windows, 0, static_cast<std::size_t>(maxX), 0, static_cast<std::size_t>(maxY), dockspaceID());
    }
}

void DockSpace::layoutInFreeRegion(const std::vector<std::vector<int>>& grid, const Windows& windows, std::size_t x0, std::size_t x1, std::size_t y0, std::size_t y1, ImGuiID nodeId) {

    // Check if entire region belongs to exactly one window ID
    assert(grid[x0][y0] >= 0);
    const std::size_t firstId = static_cast<std::size_t>(grid[x0][y0]);
    const bool        allSame = std::ranges::all_of( // x in [x0, x1)
        std::views::iota(x0, x1), [&](std::size_t x) {
            return std::ranges::all_of( // y in [y0, y1)
                std::views::iota(y0, y1), [&](std::size_t y) { return grid[x][y] == firstId; });
        });

    if (allSame) {
        // last window, occupy whatever is left
        if (firstId < windows.size()) {
            ImGui::DockBuilderDockWindow(windows[firstId]->name.c_str(), nodeId);
        } else {
            ImGui::DockBuilderDockWindow(std::to_string(firstId).c_str(), nodeId); // empty draw
        }
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(nodeId);
        node->SetLocalFlags(nodeFlags());
        return;
    }

    // Try vertical split: left/right
    for (std::size_t cutX = x0 + 1; cutX < x1; cutX++) {
        const bool canSplit = std::ranges::all_of(std::views::iota(y0, y1), [&](auto y) { return grid[cutX][y] != grid[cutX - 1][y]; });
        if (canSplit) {
            float   fraction = float(cutX - x0) / float(x1 - x0);
            ImGuiID leftNode, rightNode;
            ImGui::DockBuilderSplitNode(nodeId, ImGuiDir_Left, fraction, &leftNode, &rightNode);
            layoutInFreeRegion(grid, windows, x0, cutX, y0, y1, leftNode);
            layoutInFreeRegion(grid, windows, cutX, x1, y0, y1, rightNode);
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
            layoutInFreeRegion(grid, windows, x0, x1, y0, cutY, topNode);
            layoutInFreeRegion(grid, windows, x0, x1, cutY, y1, bottomNode);
            return;
        }
    }
}

void DockSpace::relayout(const Windows& windows) {
    const ImGuiID dockspaceID = this->dockspaceID();
    ImGui::DockBuilderAddNode(dockspaceID);
    ImGui::DockBuilderSetNodeSize(dockspaceID, ImGui::GetWindowSize());

    if (isBoxLayout()) {
        layoutInBox(windows, _layoutType == DockingLayoutType::Row ? ImGuiDir_Left : ImGuiDir_Up);
    } else if (isFreeLayout()) {
        layoutInFree(windows);
    } else {
        layoutInGrid(windows);
    }

    ImGui::DockBuilderFinish(dockspaceID);

    _needsRelayout = false;
}

void DockSpace::setNeedsRelayout(bool needs) { _needsRelayout = needs; }

bool DockSpace::isFreeLayout() const { return _layoutType == DockingLayoutType::Free; }
bool DockSpace::isBoxLayout() const { return _layoutType == DockingLayoutType::Row || _layoutType == DockingLayoutType::Column; }

int DockSpace::nodeFlags() const {
    int flags = ImGuiDockNodeFlags_None;

    if (isFreeLayout()) {
        // TODO: ImGui bug: When detached and redocked, the window menu button comes back
        // TODO: Plot-Tabs are currently always hidden. In the future, they might be used for Mode::Layout, but because the mode must be passed to this function (which is tricky right now),
        //  the Docking feature for layout is not yet available.
        flags |= ImGuiDockNodeFlags_NoWindowMenuButton;
        flags |= ImGuiDockNodeFlags_NoUndocking;
        flags |= ImGuiDockNodeFlags_HiddenTabBar;
        flags |= ImGuiDockNodeFlags_NoTabBar;
    } else {
        flags |= ImGuiDockNodeFlags_NoUndocking;
        flags |= ImGuiDockNodeFlags_HiddenTabBar;
        flags |= ImGuiDockNodeFlags_NoTabBar;
    }

    return flags;
}
