#include "FlowgraphLayout.hpp"

#include <boost/ut.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
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

int main() { return 0; }
