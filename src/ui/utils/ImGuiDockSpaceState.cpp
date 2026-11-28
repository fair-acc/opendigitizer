#include "ImGuiDockSpaceState.hpp"

#include <array>
#include <print>
#include <string>
#include <unordered_map>

#include <imgui_internal.h>

using namespace gr::od;

namespace {
using namespace dock_space;

constexpr auto kDefaultSplitRatio = 1.F;

gr::property_map saveLayoutVisitor(ImGuiDockNode* node, std::unordered_map<std::string_view, ImGuiWindow*>& remainingWindowsByName) noexcept {
    const auto [left, right] = node->ChildNodes;

    if (node->Windows.size() > 0) {
        // only floating windows can share a node by docking onto the window frame: not saved
        return {};
    }
    if (!left && !right && node->Windows.size() == 0UL) {
        // no children and no windows, probably the empty root node: nothing to remember
        return {};
    }

    gr::property_map out;

    // children share the size less the separator: a ratio of the node's size shrinks child 1 per save
    const auto splitRatio = !left && !right ? kDefaultSplitRatio : [node, left, right] {
        if (node->SplitAxis == ImGuiAxis_X) {
            return left && right ? left->Size.x / (left->Size.x + right->Size.x) : (left ? left->Size.x : right->Size.x) / node->Size.x;
        } else {
            return left && right ? right->Size.y / (left->Size.y + right->Size.y) : (right ? right->Size.y : left->Size.y) / node->Size.y;
        }
    }();

    if (splitRatio != kDefaultSplitRatio) {
        out.try_emplace(kRatio, splitRatio);
    }

    const auto branch = [&remainingWindowsByName](auto* child) {
        gr::pmt::Value childBranch;
        if (child) {
            if (child->ChildNodes[0] == nullptr && child->ChildNodes[1] == nullptr && child->Windows.size() == 1) {
                childBranch = child->Windows.front()->Name;
                remainingWindowsByName.erase(child->Windows.front()->Name);
            } else {
                childBranch = saveLayoutVisitor(child, remainingWindowsByName);
            }
        }
        return childBranch;
    };

    if (auto firstChild = branch(left)) {
        out.try_emplace(kFirst, std::move(firstChild));
    }
    if (auto secondChild = branch(right)) {
        out.try_emplace(kSecond, std::move(secondChild));
    }

    return gr::property_map{{node->SplitAxis == ImGuiAxis_X ? kHSplit : kVSplit, std::move(out)}};
};

void applyLayoutVisitor(const gr::property_map& region, ImGuiID nodeId) noexcept {
    if (const auto split = findSplit(region)) {
        const bool  splitIsHorizontal = split->horizontal;
        const auto& regionInner       = split->inner;
        const auto  ratioIter         = regionInner.find(kRatio);
        const float ratio             = ratioIter != regionInner.end() ? ratioIter->second.value_or(kDefaultSplitRatio) : kDefaultSplitRatio;

        const auto firstIter  = regionInner.find(kFirst);
        const auto secondIter = regionInner.find(kSecond);
        if (firstIter != regionInner.end() && secondIter != regionInner.end()) {
            ImGuiID topNode;
            ImGuiID bottomNode;
            ImGui::DockBuilderSplitNode(nodeId, splitIsHorizontal ? ImGuiDir_Left : ImGuiDir_Down, ratio, &topNode, &bottomNode);

            const auto doChild = [](const auto& iter, ImGuiID node) {
                if (const auto map = iter->second.template get_if<gr::property_map>()) {
                    applyLayoutVisitor(*map, node);
                } else if (const auto windowNameStr = iter->second.template get_if<std::string_view>()) {
                    const std::string windowName(*windowNameStr);
                    ImGui::DockBuilderDockWindow(windowName.c_str(), node);
                }
            };

            doChild(firstIter, splitIsHorizontal ? topNode : bottomNode);
            doChild(secondIter, splitIsHorizontal ? bottomNode : topNode);
        }
    }
};
} // namespace

namespace gr::od {
std::optional<dock_space::Split> dock_space::findSplit(const gr::property_map& region) noexcept {
    const auto hsplit = region.find(kHSplit);
    const auto vsplit = region.find(kVSplit);
    if (hsplit == region.end() && vsplit == region.end()) {
        return std::nullopt;
    }
    const bool horizontal = hsplit != region.end();
    if (const auto inner = (horizontal ? hsplit : vsplit)->second.get_if<gr::property_map>()) {
        return Split{.horizontal = horizontal, .inner = *inner};
    }
    return std::nullopt;
}

/// custom serialisation: ImGui::SaveIniSettingsToMemory() and LoadIniSettingsFromMemory() mostly handle this,
/// but would put an entire INI file into the dashboard yaml, without control over what gets serialised
gr::property_map saveDockSpaceState(std::span<const std::string_view> relevantWindowsNames, ImGuiID dockspaceNodeID) noexcept {
    ImGuiContext&  g                    = *GImGui;
    ImGuiDockNode* centralDockspaceNode = ImGui::DockBuilderGetNode(dockspaceNodeID);
    assert(centralDockspaceNode);

    std::unordered_map<std::string_view, ImGuiWindow*> remainingWindowsByName;
    remainingWindowsByName.reserve(relevantWindowsNames.size());
    for (std::string_view windowName : relevantWindowsNames) {
        [[maybe_unused]] auto [_, wasEmplaced] = remainingWindowsByName.try_emplace(windowName, nullptr);
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
            {dock_space::kX, windowPtr->Pos.x},
            {dock_space::kY, windowPtr->Pos.y},
            {dock_space::kWidth, windowPtr->Size.x},
            {dock_space::kHeight, windowPtr->Size.y},
        };
    }

    return {
        {dock_space::kDockSpace, std::move(dockSpace)},
        {dock_space::kFloatingWindows, std::move(floatingWindows)},
    };
}

bool restoreDockSpaceState(const gr::property_map& state, ImGuiID rootNodeID) noexcept {
    auto dockSpaceIter = state.find(dock_space::kDockSpace);
    auto floatingIter  = state.find(dock_space::kFloatingWindows);
    if (floatingIter == state.end() || dockSpaceIter == state.end()) {
        return false;
    }

    bool docked = false;
    if (const auto dockMap = dockSpaceIter->second.get_if<gr::property_map>(); dockMap && (dockMap->contains(dock_space::kHSplit) || dockMap->contains(dock_space::kVSplit))) {
        applyLayoutVisitor(*dockMap, rootNodeID);
        docked = true;
    }

    if (const auto floatingWindowsMap = floatingIter->second.get_if<gr::property_map>()) {
        for (const auto& [windowName, windowDescriptionPmtValue] : *floatingWindowsMap) {
            if (const auto windowDescription = windowDescriptionPmtValue.get_if<gr::property_map>()) {
                auto get = [windowDescription](const auto& key) -> float {
                    auto iter = windowDescription->find(key);
                    return iter == std::end(*windowDescription) ? 0.F : iter->second.value_or(0.F);
                };

                const std::string name(windowName);
                ImGui::SetWindowPos(name.c_str(), {get(dock_space::kX), get(dock_space::kY)});
                ImGui::SetWindowSize(name.c_str(), {get(dock_space::kWidth), get(dock_space::kHeight)});
            }
        }
    }
    return docked;
}
} // namespace gr::od
