#include "Docking.hpp"
#include "../ui/common/ImguiWrap.hpp"

#include <fmt/format.h>

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
        if (type == DockingLayoutType::Free) {
            // No need to relayout, just show the titlebar so user can drag it
            setFlagsForAllDockNodes(nodeFlags());
        } else {
            setNeedsRelayout(true);
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
        window->setGeometry(ImGui::GetWindowPos(), ImGui::GetWindowSize());

        if (layoutType() == DockingLayoutType::Free) {
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

        if (window->hasSize()) {
            ImGui::DockBuilderSetNodeSize(nodeId, ImVec2(float(window->width), node->Size.y));
        }
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

void DockSpace::relayout(const Windows& windows) {
    clearWindowGeometry(windows);

    const ImGuiID dockspaceID = this->dockspaceID();
    ImGui::DockBuilderAddNode(dockspaceID);
    ImGui::DockBuilderSetNodeSize(dockspaceID, ImGui::GetWindowSize());

    if (isBoxLayout()) {
        layoutInBox(windows, _layoutType == DockingLayoutType::Row ? ImGuiDir_Left : ImGuiDir_Up);
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
        // No central docking (tabbed) to match old behavior, but we might want to enable.
        // TODO: ImGui bug: When detached and redocked, the window menu button comes back
        flags |= ImGuiDockNodeFlags_NoWindowMenuButton;
    } else {
        flags |= ImGuiDockNodeFlags_NoUndocking;
        flags |= ImGuiDockNodeFlags_HiddenTabBar;
        flags |= ImGuiDockNodeFlags_NoTabBar;
    }

    return flags;
}
