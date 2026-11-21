#ifndef OPENDIGITIZER_UTILS_IMGUI_DOCK_SPACE_LAYOUT_SERIALIZATION_HPP
#define OPENDIGITIZER_UTILS_IMGUI_DOCK_SPACE_LAYOUT_SERIALIZATION_HPP

#include <gnuradio-4.0/Tag.hpp>
#include <imgui.h>
#include <optional>
#include <span>

namespace gr::od {

namespace dock_space {
inline constexpr const char* kDockSpace       = "dockSpace";
inline constexpr const char* kFloatingWindows = "floatingWindows";
inline constexpr const char* kHSplit          = "hsplit";
inline constexpr const char* kVSplit          = "vsplit";
inline constexpr const char* kRatio           = "ratio";
inline constexpr const char* kFirst           = "first";
inline constexpr const char* kSecond          = "second";
inline constexpr const char* kX               = "x";
inline constexpr const char* kY               = "y";
inline constexpr const char* kWidth           = "width";
inline constexpr const char* kHeight          = "height";

struct Split {
    bool             horizontal = true;
    gr::property_map inner;
};

[[nodiscard]] std::optional<Split> findSplit(const gr::property_map& region) noexcept;
} // namespace dock_space
/// saves the state of all relevant windows and how they are docked within the dockspace, and of the floating windows named in relevantWindowsNames
gr::property_map saveDockSpaceState(std::span<const std::string_view> relevantWindowsNames, ImGuiID dockspaceNodeID) noexcept;

/// restores the state of a dockspace; only between ImGui::DockBuilderAddNode() and ImGui::DockBuilderFinish()
[[nodiscard]] bool restoreDockSpaceState(const gr::property_map& state, ImGuiID rootNodeID) noexcept; // true: windows were docked
} // namespace gr::od

#endif
