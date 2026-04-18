#ifndef OPENDIGITIZER_UTILS_IMGUI_DOCK_SPACE_LAYOUT_SERIALIZATION_HPP
#define OPENDIGITIZER_UTILS_IMGUI_DOCK_SPACE_LAYOUT_SERIALIZATION_HPP

#include <gnuradio-4.0/Tag.hpp>
#include <imgui.h>
#include <span>

namespace DigitizerUi {
/// Save the state of all relevant windows and how they are docked within a given
/// dockspace. Additionally saves the state of any floating windows whose names
/// are found in relevantWindowsNames
gr::property_map saveDockSpaceState(std::span<const std::string_view> relevantWindowsNames, ImGuiID dockspaceNodeID) noexcept;

/// Restore the state of a dockspace, should be called within ImGui::DockBuilderAddNode()
/// and ImGui::DockBuilderFinish().
void restoreDockSpaceState(const gr::property_map& state, ImGuiID rootNodeID) noexcept;
} // namespace DigitizerUi

#endif
