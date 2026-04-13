#include "ImGuiDockSpaceState.hpp"

#include <array>
#include <print>
#include <string>
#include <unordered_map>

#include <imgui_internal.h>

namespace {
constexpr auto defaultSplitRatio = 1.F;

gr::property_map saveLayoutVisitor(ImGuiDockNode* node, std::unordered_map<std::string_view, ImGuiWindow*>& remainingWindowsByName) noexcept {
    const auto [left, right] = node->ChildNodes;

    if (node->Windows.size() > 0) {
        // this seems to only really be possible for floating windows, you can dock
        // onto the window frame and both windows will take up the same node. We
        // just don't save this
        return {};
    }
    if (!left && !right && node->Windows.size() == 0UL) {
        // this node has no children or windows so it does not need to be
        // remembered, probably the root node with nothing docked in it
        return {};
    }

    gr::property_map out;

    const auto splitRatio = !left && !right ? defaultSplitRatio : [node, left, right] {
        if (node->SplitAxis == ImGuiAxis_X) {
            return (left ? left->Size.x : right->Size.x) / node->Size.x;
        } else {
            return (right ? right->Size.y : left->Size.y) / node->Size.y;
        }
    }();

    if (splitRatio != defaultSplitRatio) {
        out.try_emplace("ratio", splitRatio);
    }

    const auto branch = [&remainingWindowsByName](auto* child) {
        gr::pmt::Value childBranch;
        if (child) {
            if (child->ChildNodes[0] == nullptr && child->ChildNodes[1] == nullptr && child->Windows.size() == 1) {
                childBranch = child->Windows.front()->Name;
                if (auto iter = remainingWindowsByName.find(child->Windows.front()->Name); iter != remainingWindowsByName.end()) {
                    remainingWindowsByName.erase(iter);
                }
            } else {
                childBranch = saveLayoutVisitor(child, remainingWindowsByName);
            }
        }
        return childBranch;
    };

    if (auto firstChild = branch(left)) {
        out.try_emplace("first", std::move(firstChild));
    }
    if (auto secondChild = branch(right)) {
        out.try_emplace("second", std::move(secondChild));
    }

    return gr::property_map{{node->SplitAxis == ImGuiAxis_X ? "hsplit" : "vsplit", std::move(out)}};
};

void applyLayoutVisitor(const gr::property_map& region, ImGuiID nodeId) noexcept {
    auto hsplit = region.find("hsplit");
    auto vsplit = region.find("vsplit");
    if (hsplit == region.end() && vsplit == region.end()) { // invalid yaml
        return;
    }
    const bool splitIsHorizontal = hsplit != region.end();
    const auto split             = splitIsHorizontal ? hsplit : vsplit;

    if (const auto* regionInner = split->second.get_if<gr::property_map>()) {
        const auto  ratioIter = regionInner->find("ratio");
        const float ratio     = ratioIter != regionInner->end() ? ratioIter->second.value_or(defaultSplitRatio) : defaultSplitRatio;

        const auto firstIter  = regionInner->find("first");
        const auto secondIter = regionInner->find("second");
        if (firstIter != regionInner->end() && secondIter != regionInner->end()) {
            ImGuiID topNode;
            ImGuiID bottomNode;
            ImGui::DockBuilderSplitNode(nodeId, splitIsHorizontal ? ImGuiDir_Left : ImGuiDir_Down, ratio, &topNode, &bottomNode);

            const auto doChild = [](const auto& iter, ImGuiID node) {
                if (const auto* map = iter->second.template get_if<gr::property_map>()) {
                    applyLayoutVisitor(*map, node);
                } else if (const auto* windowNameStr = iter->second.template get_if<std::pmr::string>()) {
                    ImGui::DockBuilderDockWindow(windowNameStr->c_str(), node);
                }
            };

            doChild(firstIter, splitIsHorizontal ? topNode : bottomNode);
            doChild(secondIter, splitIsHorizontal ? bottomNode : topNode);
        }
    }
};
} // namespace

namespace DigitizerUi {
/// Custom serialization of ImGui dockspace state. ImGui::SaveIniSettingsToMemory()
/// and ImGui::LoadIniSettingsFromMemory() are also available and they mostly
/// handle this, but then we end up doing something like putting an entire INI file
/// as a string into our dashboard yaml, and we can't control what gets serialized.
gr::property_map saveDockSpaceState(std::span<const std::string_view> relevantWindowsNames, ImGuiID dockspaceNodeID) noexcept {
    ImGuiContext&  g                    = *GImGui;
    ImGuiDockNode* centralDockspaceNode = ImGui::DockBuilderGetNode(dockspaceNodeID);
    assert(centralDockspaceNode);

    // pair up relevant window names with their imgui windows
    std::unordered_map<std::string_view, ImGuiWindow*> remainingWindowsByName;
    remainingWindowsByName.reserve(relevantWindowsNames.size());
    for (std::string_view windowName : relevantWindowsNames) {
        auto [_, wasEmplaced] = remainingWindowsByName.try_emplace(windowName, nullptr);
        assert(wasEmplaced); // non-unique window names
    }
    for (ImGuiWindow* window : g.Windows) {
        if (auto iter = remainingWindowsByName.find(window->Name); iter != std::end(remainingWindowsByName)) {
            assert(iter->second == nullptr); // non-unique window names, not in relevantWindowsNames, but in imgui
            iter->second = window;
        }
    }

    gr::property_map dockSpace = saveLayoutVisitor(centralDockspaceNode, remainingWindowsByName);

    gr::property_map floatingWindows;
    for (const auto [windowName, windowPtr] : remainingWindowsByName) {
        if (windowPtr == nullptr) {
#ifndef NDEBUG
            std::println(stderr, "WARNING: attempt to save state for window {} but no such window exists, or it was deemed invalid", windowName);
#endif
            continue;
        }

        floatingWindows[std::pmr::string{windowName}] = gr::property_map{
            {"x", windowPtr->Pos.x},
            {"y", windowPtr->Pos.y},
            {"width", windowPtr->Size.x},
            {"height", windowPtr->Size.y},
        };
    }

    return {
        {"dockSpace", std::move(dockSpace)},
        {"floatingWindows", std::move(floatingWindows)},
    };
}

void restoreDockSpaceState(const gr::property_map& state, ImGuiID rootNodeID) noexcept {
    auto dockSpaceIter = state.find("dockSpace");
    auto floatingIter  = state.find("floatingWindows");
    if (floatingIter == state.end() || dockSpaceIter == state.end()) {
        return;
    }

    if (const auto* dockMap = dockSpaceIter->second.get_if<gr::property_map>()) {
        applyLayoutVisitor(*dockMap, rootNodeID);
    }

    if (const auto* floatingWindowsMap = floatingIter->second.get_if<gr::property_map>()) {
        for (const auto& [windowName, windowDescriptionPmtValue] : *floatingWindowsMap) {
            if (const auto* windowDescription = windowDescriptionPmtValue.get_if<gr::property_map>()) {
                auto get = [windowDescription](const auto& key) -> float {
                    auto iter = windowDescription->find(key);
                    return iter == std::end(*windowDescription) ? 0.F : iter->second.value_or(0.F);
                };

                ImGui::SetWindowPos(windowName.c_str(), {get("x"), get("y")});
                ImGui::SetWindowSize(windowName.c_str(), {get("width"), get("height")});
            }
        }
    }
}
} // namespace DigitizerUi
