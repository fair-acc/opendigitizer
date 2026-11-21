#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace gr::od::flowgraph_layout {

struct Size {
    float width  = 0.f;
    float height = 0.f;
};

struct Position {
    float x = 0.f;
    float y = 0.f;
};

struct Edge {
    std::size_t source = 0UZ;
    std::size_t target = 0UZ;

    // displayed port centres, from the top of their nodes
    float sourcePortY = 0.f;
    float targetPortY = 0.f;
};

struct Options {
    float       horizontalNodeSpacing      = 120.f;
    float       verticalNodeSpacing        = 32.f;
    float       verticalComponentSpacing   = 80.f;
    float       padding                    = 16.f;
    std::size_t crossingMinimizationSweeps = 4UZ;
    std::size_t coordinateAssignmentSweeps = 8UZ;
};

[[nodiscard]] std::vector<Position> compute(const std::vector<Size>& nodeSizes, const std::vector<Edge>& edges, const Options& options = {});

struct WrappedRows {
    std::vector<Position> offsets;       // each block's top left point as an offset from the overall region's top left
    Size                  containerSize; // total size of the area all the blocks fit into
};

/// places the nodes left to right in a container, `gap` apart, wrapping beyond preferredWidth;
/// they grow the container instead of wrapping when minimumContainerWidth is smaller than the gaps plus the first minimumNodesOnFirstRow nodes
[[nodiscard]] WrappedRows computeFixedBlockWrappingWithGap(std::span<const Size> nodeSizes, float minimumContainerWidth, float gap, std::size_t minimumNodesOnFirstRow);

} // namespace gr::od::flowgraph_layout
