#include "FlowgraphLayout.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace DigitizerUi::flowgraph_layout {
namespace {

using Indices = std::vector<std::size_t>;

// Move blocks towards their target positions while preserving their order and minimum spacing.
void compactLayer(const Indices& layer, const std::vector<float>& layerOffsetsY, const std::vector<float>& desiredY, std::vector<Position>& positions) {
    struct Run {
        std::size_t begin;
        std::size_t end;
        float       sum;

        [[nodiscard]] float mean() const { return sum / static_cast<float>(end - begin); }
    };

    std::vector<Run> runs;
    runs.reserve(layer.size());
    for (std::size_t i = 0UZ; i < layer.size(); i++) {
        const auto node = layer[i];
        runs.push_back({i, i + 1UZ, desiredY[node] - layerOffsetsY[node]});
        while (runs.size() > 1UZ && runs[runs.size() - 2UZ].mean() > runs.back().mean()) {
            const auto last = runs.back();
            runs.pop_back();
            runs.back().end = last.end;
            runs.back().sum += last.sum;
        }
    }
    for (const auto& run : runs) {
        for (auto i = run.begin; i < run.end; i++) {
            const auto node   = layer[i];
            positions[node].y = layerOffsetsY[node] + run.mean();
        }
    }
}

} // namespace

// Simplified Sugiyama-style layered layout.
// Sugiyama, Tagawa and Toda (1981): https://doi.org/10.1109/TSMC.1981.4308636
// See also GR4's layoutSugiyama() in algorithm/include/gnuradio-4.0/algorithm/ImGraph.hpp.
std::vector<Position> compute(const std::vector<Size>& nodeSizes, const std::vector<Edge>& edges, const Options& options) {
    std::vector<Position> positions(nodeSizes.size());
    if (nodeSizes.empty()) {
        return positions;
    }

    // Collect incoming and outgoing connections for each block.
    std::vector<Indices> incoming(nodeSizes.size());
    std::vector<Indices> outgoing(nodeSizes.size());
    for (std::size_t i = 0UZ; i < edges.size(); i++) {
        const auto& edge = edges[i];
        if (edge.source < nodeSizes.size() && edge.target < nodeSizes.size() && edge.source != edge.target) {
            outgoing[edge.source].push_back(i);
            incoming[edge.target].push_back(i);
        }
    }

    // DFS feedback-edge detection: mark connections that return to a block on the current path.
    enum class Visit { Unseen, Active, Finished };
    struct Frame {
        std::size_t node;
        std::size_t nextEdge = 0UZ;
    };
    std::vector<Visit> visited(nodeSizes.size(), Visit::Unseen);
    std::vector<bool>  feedbackEdges(edges.size(), false);
    std::vector<Frame> dfsStack;
    Indices            finishOrder;
    for (std::size_t root = 0UZ; root < nodeSizes.size(); root++) {
        if (visited[root] != Visit::Unseen) {
            continue;
        }
        visited[root] = Visit::Active;
        dfsStack.push_back({root});
        while (!dfsStack.empty()) {
            auto& frame = dfsStack.back();
            if (frame.nextEdge == outgoing[frame.node].size()) {
                visited[frame.node] = Visit::Finished;
                finishOrder.push_back(frame.node);
                dfsStack.pop_back();
                continue;
            }
            const auto edgeIndex = outgoing[frame.node][frame.nextEdge++];
            const auto target    = edges[edgeIndex].target;
            if (visited[target] == Visit::Active) {
                feedbackEdges[edgeIndex] = true;
            } else if (visited[target] == Visit::Unseen) {
                visited[target] = Visit::Active;
                dfsStack.push_back({target});
            }
        }
    }

    // Longest-path ranking: ignore feedback edges and place each block one column after its rightmost predecessor.
    std::vector<std::size_t> rank(nodeSizes.size(), 0UZ);
    for (const auto node : finishOrder | std::views::reverse) {
        for (const auto edgeIndex : outgoing[node]) {
            if (!feedbackEdges[edgeIndex]) {
                const auto target = edges[edgeIndex].target;
                rank[target]      = std::max(rank[target], rank[node] + 1UZ);
            }
        }
    }

    // Find connected groups so separate flowgraphs can be placed independently.
    std::vector<Indices> components;
    std::vector<bool>    assigned(nodeSizes.size(), false);
    for (std::size_t root = 0UZ; root < nodeSizes.size(); root++) {
        if (assigned[root]) {
            continue;
        }
        auto& component = components.emplace_back();
        component.push_back(root);
        assigned[root] = true;
        auto append    = [&](std::size_t node) {
            if (!assigned[node]) {
                assigned[node] = true;
                component.push_back(node);
            }
        };
        for (std::size_t i = 0UZ; i < component.size(); i++) {
            const auto node = component[i];
            for (const auto edgeIndex : incoming[node]) {
                append(edges[edgeIndex].source);
            }
            for (const auto edgeIndex : outgoing[node]) {
                append(edges[edgeIndex].target);
            }
        }
        std::ranges::sort(component);
    }

    std::vector<std::size_t> order(nodeSizes.size(), 0UZ);
    std::vector<float>       scores(nodeSizes.size());
    std::vector<float>       layerOffsetsY(nodeSizes.size());
    std::vector<float>       desiredY(nodeSizes.size());
    std::vector<float>       neighbourTargetsY;
    float                    componentY = options.padding;
    for (const auto& component : components) {
        // Barycentre ordering: reorder each column to reduce connection crossings.
        const auto           maxRank = std::ranges::max(component | std::views::transform([&](auto node) { return rank[node]; }));
        std::vector<Indices> layers(maxRank + 1UZ);
        std::vector<Indices> outgoingByLayer(maxRank + 1UZ);
        for (const auto node : component) {
            order[node] = layers[rank[node]].size();
            layers[rank[node]].push_back(node);
            for (const auto edgeIndex : outgoing[node]) {
                if (!feedbackEdges[edgeIndex]) {
                    outgoingByLayer[rank[node]].push_back(edgeIndex);
                }
            }
        }

        auto portOrder = [&](std::size_t node, float portY) { return static_cast<float>(order[node]) + portY / std::max(nodeSizes[node].height, 1.f); };
        // Estimate crossings between connections joining the same pair of columns.
        auto countCrossings = [&] {
            std::size_t count = 0UZ;
            for (const auto& edgeGroup : outgoingByLayer) {
                for (std::size_t i = 0UZ; i < edgeGroup.size(); i++) {
                    const auto& first = edges[edgeGroup[i]];
                    for (std::size_t j = i + 1UZ; j < edgeGroup.size(); j++) {
                        const auto& second = edges[edgeGroup[j]];
                        if (rank[first.target] != rank[second.target]) {
                            continue;
                        }
                        const float sourceDifference = portOrder(first.source, first.sourcePortY) - portOrder(second.source, second.sourcePortY);
                        const float targetDifference = portOrder(first.target, first.targetPortY) - portOrder(second.target, second.targetPortY);
                        count += sourceDifference * targetDifference < 0.f;
                    }
                }
            }
            return count;
        };

        auto visitNeighbours = [&](std::size_t node, bool predecessors, auto&& visit) {
            for (const auto edgeIndex : predecessors ? incoming[node] : outgoing[node]) {
                if (feedbackEdges[edgeIndex]) {
                    continue;
                }
                const auto& edge = edges[edgeIndex];
                visit(predecessors ? edge.source : edge.target, predecessors ? edge.targetPortY : edge.sourcePortY, predecessors ? edge.sourcePortY : edge.targetPortY);
            }
        };
        auto sweepLayers = [&](bool predecessors, auto&& update) {
            for (std::size_t i = 1UZ; i < layers.size(); i++) {
                update(layers[predecessors ? i : layers.size() - 1UZ - i]);
            }
        };

        // Keep the best order because later passes can increase crossings.
        auto bestLayers    = layers;
        auto bestCrossings = countCrossings();
        for (std::size_t sweep = 0UZ; sweep < options.crossingMinimizationSweeps && bestCrossings > 0UZ; sweep++) {
            // Try left-to-right and right-to-left passes.
            for (const bool predecessors : {true, false}) {
                sweepLayers(predecessors, [&](Indices& layer) {
                    // Score each block by the average vertical order of its neighbours' ports.
                    for (const auto node : layer) {
                        float       sum   = 0.f;
                        std::size_t count = 0UZ;
                        visitNeighbours(node, predecessors, [&](std::size_t neighbour, float, float portY) {
                            sum += portOrder(neighbour, portY) - 0.5f;
                            count++;
                        });
                        scores[node] = count == 0UZ ? static_cast<float>(order[node]) : sum / static_cast<float>(count);
                    }
                    std::ranges::stable_sort(layer, {}, [&](auto node) { return scores[node]; });
                    for (std::size_t i = 0UZ; i < layer.size(); i++) {
                        order[layer[i]] = i;
                    }
                });
                if (const auto crossings = countCrossings(); crossings < bestCrossings) {
                    bestCrossings = crossings;
                    bestLayers    = layers;
                }
                if (bestCrossings == 0UZ) {
                    break;
                }
            }
        }
        layers = std::move(bestLayers);

        // Set column widths and centre each column vertically.
        std::vector<float> layerHeights;
        Size               componentSize;
        for (const auto& layer : layers) {
            float width  = 0.f;
            float height = 0.f;
            for (const auto node : layer) {
                positions[node].x   = componentSize.width;
                layerOffsetsY[node] = height;
                width               = std::max(width, nodeSizes[node].width);
                height += nodeSizes[node].height + options.verticalNodeSpacing;
            }
            height -= options.verticalNodeSpacing;
            layerHeights.push_back(height);
            componentSize.width += width + options.horizontalNodeSpacing;
            componentSize.height = std::max(componentSize.height, height);
        }
        for (const auto& [layer, height] : std::views::zip(layers, layerHeights)) {
            for (const auto node : layer) {
                positions[node].y = layerOffsetsY[node] + (componentSize.height - height) * 0.5f;
            }
        }

        // Median + compaction: align connected ports while keeping blocks in order and separated.
        const auto median = [](std::vector<float>& values) {
            std::ranges::sort(values);
            const auto middle = values.size() / 2UZ;
            return values.size() % 2UZ == 0UZ ? (values[middle - 1UZ] + values[middle]) * 0.5f : values[middle];
        };
        for (std::size_t sweep = 0UZ; sweep < options.coordinateAssignmentSweeps; sweep++) {
            for (const bool predecessors : {true, false}) {
                sweepLayers(predecessors, [&](const Indices& layer) {
                    for (const auto node : layer) {
                        neighbourTargetsY.clear();
                        visitNeighbours(node, predecessors, [&](std::size_t neighbour, float ownPortY, float neighbourPortY) { neighbourTargetsY.push_back(positions[neighbour].y + neighbourPortY - ownPortY); });
                        desiredY[node] = neighbourTargetsY.empty() ? positions[node].y : median(neighbourTargetsY);
                    }
                    compactLayer(layer, layerOffsetsY, desiredY, positions);
                });
            }
        }

        // Stack each connected group below the previous one.
        const auto minY      = std::ranges::min(component | std::views::transform([&](auto node) { return positions[node].y; }));
        componentSize.height = 0.f;
        for (const auto node : component) {
            positions[node].y -= minY;
            componentSize.height = std::max(componentSize.height, positions[node].y + nodeSizes[node].height);
            positions[node].x += options.padding;
            positions[node].y += componentY;
        }
        componentY += componentSize.height + options.verticalComponentSpacing;
    }

    return positions;
}

} // namespace DigitizerUi::flowgraph_layout
