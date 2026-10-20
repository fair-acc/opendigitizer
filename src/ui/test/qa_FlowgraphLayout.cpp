#include "FlowgraphLayout.hpp"

#include <boost/ut.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <ranges>
#include <vector>

using namespace boost::ut;
using namespace DigitizerUi::flowgraph_layout;

namespace {

void expectValidLayout(const std::vector<Size>& sizes, const std::vector<Position>& positions) {
    expect(eq(positions.size(), sizes.size())) << fatal;
    for (const auto& position : positions) {
        expect(std::isfinite(position.x) && std::isfinite(position.y)) << "Block coordinates are finite";
    }
    for (std::size_t i = 0UZ; i < sizes.size(); i++) {
        for (std::size_t j = i + 1UZ; j < sizes.size(); j++) {
            const auto& first     = positions[i];
            const auto& second    = positions[j];
            const bool  separated = first.x + sizes[i].width <= second.x || second.x + sizes[j].width <= first.x || first.y + sizes[i].height <= second.y || second.y + sizes[j].height <= first.y;
            expect(separated) << "Blocks " << i << " and " << j << " overlap";
        }
    }
}

} // namespace

const boost::ut::suite<"FlowgraphLayout"> layoutTests = [] {
    "branching graph"_test = [] {
        enum : std::size_t { Source, A, B, C, D };
        const std::vector<Size> sizes = {{120.f, 80.f}, {160.f, 100.f}, {90.f, 60.f}, {130.f, 120.f}, {100.f, 70.f}};
        // Initial order A/B and C/D crosses A -> D with B -> C.
        const std::vector<Edge> edges = {{Source, A, 30.f, 30.f}, {Source, B, 30.f, 30.f}, {A, D, 30.f, 30.f}, {B, C, 30.f, 30.f}};

        const auto positions = compute(sizes, edges);
        expectValidLayout(sizes, positions);

        for (const auto& edge : edges) {
            expect(lt(positions[edge.source].x + sizes[edge.source].width, positions[edge.target].x)) << "Connections flow left to right";
        }

        const float branchOrder      = positions[A].y - positions[B].y;
        const float destinationOrder = positions[D].y - positions[C].y;
        expect(gt(branchOrder * destinationOrder, 0.f)) << "Connected branches keep the same vertical order";
    };

    "feedback loop"_test = [] {
        enum : std::size_t { A, B, C };
        const std::vector<Size> sizes(3UZ, {100.f, 60.f});
        const std::vector<Edge> edges = {{A, B, 30.f, 30.f}, {B, C, 30.f, 30.f}, {C, A, 30.f, 30.f}};

        expectValidLayout(sizes, compute(sizes, edges));
    };

    "disconnected graphs"_test = [] {
        enum : std::size_t { A, B, C, D };
        const std::vector<Size> sizes = {{100.f, 60.f}, {140.f, 100.f}, {120.f, 80.f}, {100.f, 60.f}};
        const std::vector<Edge> edges = {{A, B, 30.f, 30.f}, {C, D, 30.f, 30.f}};

        const auto positions = compute(sizes, edges);
        expectValidLayout(sizes, positions);

        for (const auto& edge : edges) {
            expect(lt(positions[edge.source].x + sizes[edge.source].width, positions[edge.target].x)) << "Connections flow left to right";
        }

        const float firstTop     = std::min(positions[A].y, positions[B].y);
        const float firstBottom  = std::max(positions[A].y + sizes[A].height, positions[B].y + sizes[B].height);
        const float secondTop    = std::min(positions[C].y, positions[D].y);
        const float secondBottom = std::max(positions[C].y + sizes[C].height, positions[D].y + sizes[D].height);
        expect(firstBottom < secondTop || secondBottom < firstTop) << "Disconnected graphs are separated vertically";
    };
};

const boost::ut::suite<"FlowgraphLayout wrapped rows"> wrappedRowsTests = [] { // NOSONAR test lambda length
    static constexpr float       kGap                    = 10.f;
    static constexpr std::size_t kMinimumNodesOnFirstRow = 3UZ;

    const auto expectPositions = [](const WrappedRows& rows, const std::vector<Position>& expected) {
        expect(eq(rows.offsets.size(), expected.size())) << fatal;
        for (std::size_t i = 0UZ; i < expected.size(); i++) {
            expect(eq(rows.offsets[i].x, expected[i].x)) << "x of node " << i;
            expect(eq(rows.offsets[i].y, expected[i].y)) << "y of node " << i;
        }
    };

    "if there are no blocks then the container should be {0,0} in size"_test = [] {
        const auto rows = computeFixedBlockWrappingWithGap({}, 500.f, kGap, kMinimumNodesOnFirstRow);
        expect(rows.offsets.empty());
        expect(eq(rows.containerSize.width, 0.f));
        expect(eq(rows.containerSize.height, 0.f));
    };

    "nodes have a gap both around and between them"_test = [expectPositions] {
        const std::vector<Size> sizes = {{100.f, 40.f}, {50.f, 60.f}};
        const auto              rows  = computeFixedBlockWrappingWithGap(sizes, 500.f, kGap, kMinimumNodesOnFirstRow);
        expectPositions(rows, {{10.f, 10.f}, {120.f, 10.f}});
        expect(eq(rows.containerSize.width, 500.f)) << "the container is the minimum width";
        expect(eq(rows.containerSize.height, kGap + std::max(sizes[0].height, sizes[1].height) + kGap)) << "container size should be gap + tallest node + gap";
    };

    "wrapping happens when many nodes are present, test row heights"_test = [expectPositions] {
        const std::vector<Size> sizes                = {{100.f, 40.f}, {100.f, 70.f}, {100.f, 40.f}, {100.f, 50.f}, {100.f, 30.f}};
        const auto              widthsOfMinimumItems = sizes | std::views::transform(&Size::width) | std::views::take(kMinimumNodesOnFirstRow);
        expect(eq(std::accumulate(widthsOfMinimumItems.begin(), widthsOfMinimumItems.end(), 0.f) + (kGap * static_cast<float>(kMinimumNodesOnFirstRow + 1)), 340.0F));
        const float enoughSpaceForMinimumNodesOnFirstRow = 400.f;
        const float firstRowHeight                       = 70.f; // largest height of block
        const float secondRowStart                       = (2 * kGap) + firstRowHeight;
        const auto  rows                                 = computeFixedBlockWrappingWithGap(sizes, enoughSpaceForMinimumNodesOnFirstRow, kGap, kMinimumNodesOnFirstRow);
        expectPositions(rows, {{kGap, kGap}, {100.f + (2 * kGap), kGap}, {200.f + (3.f * kGap), kGap}, {10.f, secondRowStart}, {120.f, secondRowStart}});
        expect(eq(rows.containerSize.width, enoughSpaceForMinimumNodesOnFirstRow)) << "minimum size was indeed enough to contain all three minimum required items";
        expect(eq(rows.containerSize.height, (kGap * 3.f) + 50.f + 70.f)) << "total height of a two row container should be three gaps plus the height of the biggest node on each row";
    };

    "if all the blocks fit exactly, then they form a single row"_test = [expectPositions] {
        const std::vector<Size> sizes       = {{100.f, 40.f}, {100.f, 40.f}, {100.f, 40.f}, {100.f, 40.f}};
        const float             gaps        = kGap * static_cast<float>(sizes.size() + 1);
        const float             nonGapsSize = 100.f * static_cast<float>(sizes.size());
        const auto              rows        = computeFixedBlockWrappingWithGap(sizes, gaps + nonGapsSize, kGap, kMinimumNodesOnFirstRow);
        expectPositions(rows, {{kGap, kGap}, {120.f, kGap}, {230.f, kGap}, {340.f, kGap}});
        expect(eq(rows.containerSize.width, gaps + nonGapsSize));
    };

    "blocks might expand the minimum width if less than 3 of them would fit in the first row"_test = [expectPositions] {
        const std::vector<Size> sizes                  = {{100.f, 40.f}, {100.f, 40.f}, {100.f, 40.f}, {100.f, 40.f}};
        const float             gapsFirstRow           = kGap * static_cast<float>(kMinimumNodesOnFirstRow + 1);
        const float             nonGapsSizeFirstRow    = 100.f * static_cast<float>(kMinimumNodesOnFirstRow);
        const float             notEnoughSpaceForThree = 150.f;
        const auto              rows                   = computeFixedBlockWrappingWithGap(sizes, notEnoughSpaceForThree, kGap, kMinimumNodesOnFirstRow);
        expectPositions(rows, {{kGap, kGap}, {120.f, kGap}, {230.f, kGap}, /* wrap to next line */ {kGap, 60.f}});
        expect(eq(rows.containerSize.width, gapsFirstRow + nonGapsSizeFirstRow)) << "the container expands to the new minimum size imposed by the few number of blocks";
        expect(eq(rows.containerSize.height, 110.f));
    };

    "an extremely wide node will expand the entire region to fit it and its gaps"_test = [expectPositions] {
        const float             hugeNodeWidth = 300.f;
        const std::vector<Size> sizes         = {{50.f, 40.f}, {50.f, 40.f}, {50.f, 40.f}, {hugeNodeWidth, 40.f}, {50.f, 40.f}};
        const auto              rows          = computeFixedBlockWrappingWithGap(sizes, 200.f, kGap, kMinimumNodesOnFirstRow);
        expectPositions(rows, {{kGap, kGap}, {70.f, kGap}, {130.f, kGap}, {kGap, 60.f}, {kGap, 110.f}});
        expect(eq(rows.containerSize.width, hugeNodeWidth + (kGap * 2.f)));
        expect(eq(rows.containerSize.height, 160.f));
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
