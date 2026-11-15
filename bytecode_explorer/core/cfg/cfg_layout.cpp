/**
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "core/cfg/cfg_layout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer {
namespace {

constexpr double DEFAULT_NODE_WIDTH = 360.0;
constexpr double NODE_HEADER_HEIGHT = 62.0;
constexpr double INSTRUCTION_ROW_HEIGHT = 28.0;
constexpr double NODE_DISCLOSURE_HEIGHT = 34.0;
constexpr double NODE_FOOTER_HEIGHT = 20.0;
constexpr uint32_t VISIBLE_INSTRUCTION_ROWS = 12U;
constexpr double PC_COLUMN_WIDTH = 68.0;
constexpr double INSTRUCTION_COLUMN_GAPS = 18.0;
constexpr double INSTRUCTION_HORIZONTAL_PADDING = 18.0;
constexpr double MIN_MNEMONIC_COLUMN_WIDTH = 120.0;
constexpr double MIN_OPERANDS_COLUMN_WIDTH = 100.0;
constexpr double MONOSPACE_CHARACTER_WIDTH = 7.5;
constexpr double MIN_ADAPTIVE_NODE_WIDTH = 360.0;
constexpr double MAX_ADAPTIVE_NODE_WIDTH = 640.0;
constexpr double MIN_NODE_WIDTH = 120.0;
constexpr double MAX_NODE_WIDTH = 2000.0;
constexpr double MIN_NODE_HEIGHT = 60.0;
constexpr double MAX_NODE_HEIGHT = 4000.0;
constexpr double CANVAS_MARGIN = 80.0;
constexpr double HORIZONTAL_GAP = 100.0;
constexpr double HANDLER_GAP = 180.0;
constexpr double VERTICAL_GAP = 130.0;
constexpr double OUTER_LANE_GAP = 48.0;
constexpr double OUTER_LANE_SPACING = 26.0;
constexpr double TERMINAL_EDGE_LENGTH = 42.0;
constexpr double OUTGOING_PORT_REGION_START = 0.6;
constexpr double INCOMING_PORT_REGION_START = 0.1;
constexpr double PORT_REGION_WIDTH = 0.3;
constexpr double COLLISION_CELL_SIZE = 4096.0;

struct NodeGeometry {
    double width {DEFAULT_NODE_WIDTH};
    double height {MIN_NODE_HEIGHT};
    std::optional<CfgPoint> pinnedPosition;
};

struct OuterEdgeInterval {
    uint32_t edge {0U};
    double start {0.0};
    double end {0.0};
};

struct OuterPortSlots {
    uint32_t incomingCount {0U};
    uint32_t outgoingCount {0U};
    uint32_t nextIncoming {0U};
    uint32_t nextOutgoing {0U};
};

double OuterPortRatio(uint32_t index, uint32_t count, bool outgoing)
{
    // Reserve separate upper/lower regions for arrivals/departures, with one slot per edge.
    const double start = outgoing ? OUTGOING_PORT_REGION_START : INCOMING_PORT_REGION_START;
    return start + PORT_REGION_WIDTH * (static_cast<double>(index) + 1.0) / (static_cast<double>(count) + 1.0);
}

CfgLayoutResult Failure(CfgLayoutStatus status, std::string diagnostic)
{
    return {status, nullptr, std::move(diagnostic)};
}

bool IsFinitePoint(const CfgPoint &point)
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool IsWithin(double value, double minimum, double maximum)
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

double DefaultNodeHeight(const BasicBlock &block)
{
    const auto visibleRows = std::min(block.instructionCount, VISIBLE_INSTRUCTION_ROWS);
    const double disclosureHeight = block.instructionCount > VISIBLE_INSTRUCTION_ROWS ? NODE_DISCLOSURE_HEIGHT : 0.0;
    return NODE_HEADER_HEIGHT + static_cast<double>(visibleRows) * INSTRUCTION_ROW_HEIGHT + disclosureHeight +
           NODE_FOOTER_HEIGHT;
}

double DefaultNodeWidth(const ControlFlowGraph &graph, const BasicBlock &block)
{
    if (graph.disassembly == nullptr || block.firstInstructionIndex >= graph.disassembly->instructions.size() ||
        block.instructionCount > graph.disassembly->instructions.size() - block.firstInstructionIndex) {
        return DEFAULT_NODE_WIDTH;
    }
    size_t mnemonicCharacters = 0U;
    size_t operandCharacters = 0U;
    for (uint32_t index = 0U; index < block.instructionCount; ++index) {
        const auto &instruction = graph.disassembly->instructions[block.firstInstructionIndex + index];
        mnemonicCharacters = std::max(mnemonicCharacters, instruction.mnemonic.size());
        operandCharacters = std::max(operandCharacters, instruction.operands.size());
    }
    const double mnemonicWidth =
        std::max(MIN_MNEMONIC_COLUMN_WIDTH, static_cast<double>(mnemonicCharacters) * MONOSPACE_CHARACTER_WIDTH);
    const double operandsWidth =
        std::max(MIN_OPERANDS_COLUMN_WIDTH, static_cast<double>(operandCharacters) * MONOSPACE_CHARACTER_WIDTH);
    return std::clamp(PC_COLUMN_WIDTH + INSTRUCTION_COLUMN_GAPS + INSTRUCTION_HORIZONTAL_PADDING + mnemonicWidth +
                          operandsWidth,
                      MIN_ADAPTIVE_NODE_WIDTH, MAX_ADAPTIVE_NODE_WIDTH);
}

bool Overlaps(const CfgNodeLayout &left, const CfgNodeLayout &right)
{
    return left.x < right.x + right.width && right.x < left.x + left.width && left.y < right.y + right.height &&
           right.y < left.y + left.height;
}

uint64_t CollisionCellKey(uint32_t x, uint32_t y)
{
    return (static_cast<uint64_t>(x) << 32U) | y;
}

// Returns true when the node overlaps an earlier cell occupant with a pinned endpoint.
bool OverlapsPinnedOccupant(const std::vector<CfgNodeLayout> &nodes, const CfgNodeLayout &node,
                            const std::vector<BasicBlockId> &occupants)
{
    for (const auto occupant : occupants) {
        if ((node.pinned || nodes[occupant].pinned) && Overlaps(node, nodes[occupant])) {
            return true;
        }
    }
    return false;
}

// Registers one node in every collision cell it covers, returning true when it overlaps
// an earlier occupant with a pinned endpoint.
bool RegisterNodeCells(const std::vector<CfgNodeLayout> &nodes, const CfgNodeLayout &node,
                       std::unordered_map<uint64_t, std::vector<BasicBlockId>> *cells)
{
    const auto firstX = static_cast<uint32_t>(node.x / COLLISION_CELL_SIZE);
    const auto lastX = static_cast<uint32_t>((node.x + node.width) / COLLISION_CELL_SIZE);
    const auto firstY = static_cast<uint32_t>(node.y / COLLISION_CELL_SIZE);
    const auto lastY = static_cast<uint32_t>((node.y + node.height) / COLLISION_CELL_SIZE);
    for (uint32_t cellX = firstX; cellX <= lastX; ++cellX) {
        for (uint32_t cellY = firstY; cellY <= lastY; ++cellY) {
            auto &occupants = (*cells)[CollisionCellKey(cellX, cellY)];
            if (OverlapsPinnedOccupant(nodes, node, occupants)) {
                return true;
            }
            occupants.push_back(node.block);
        }
    }
    return false;
}

bool HasPinnedOverlap(const std::vector<CfgNodeLayout> &nodes)
{
    if (std::none_of(nodes.begin(), nodes.end(), [](const auto &node) { return node.pinned; })) {
        return false;
    }
    std::unordered_map<uint64_t, std::vector<BasicBlockId>> cells;
    cells.reserve(nodes.size());
    for (const auto &node : nodes) {
        if (RegisterNodeCells(nodes, node, &cells)) {
            return true;
        }
    }
    return false;
}

bool SegmentCrossesNodeInterior(const CfgPoint &start, const CfgPoint &end, const CfgNodeLayout &node)
{
    if (start.x == end.x) {
        const double low = std::min(start.y, end.y);
        const double high = std::max(start.y, end.y);
        return start.x > node.x && start.x < node.x + node.width && low < node.y + node.height && high > node.y;
    }
    if (start.y == end.y) {
        const double low = std::min(start.x, end.x);
        const double high = std::max(start.x, end.x);
        return start.y > node.y && start.y < node.y + node.height && low < node.x + node.width && high > node.x;
    }
    return true;
}

class NodeSpatialIndex final {
public:
    explicit NodeSpatialIndex(const std::vector<CfgNodeLayout> &nodes) : nodes_(nodes)
    {
        cells_.reserve(nodes.size());
        for (const auto &node : nodes) {
            ForEachCell(node.x, node.y, node.x + node.width, node.y + node.height,
                        [this, &node](uint64_t key) { cells_[key].push_back(node.block); });
        }
    }

    bool SegmentCrossesOtherNode(const CfgPoint &start, const CfgPoint &end, BasicBlockId owner) const
    {
        bool crosses = false;
        ForEachCell(std::min(start.x, end.x), std::min(start.y, end.y), std::max(start.x, end.x),
                    std::max(start.y, end.y), [this, &crosses, &owner, &start, &end](uint64_t key) {
                        if (crosses) {
                            return;
                        }
                        const auto found = cells_.find(key);
                        if (found == cells_.end()) {
                            return;
                        }
                        for (const auto block : found->second) {
                            if (block != owner && SegmentCrossesNodeInterior(start, end, nodes_[block])) {
                                crosses = true;
                                return;
                            }
                        }
                    });
        return crosses;
    }

private:
    template <class Callback>
    static void ForEachCell(double left, double top, double right, double bottom, Callback callback)
    {
        const auto firstX = static_cast<uint32_t>(left / COLLISION_CELL_SIZE);
        const auto lastX = static_cast<uint32_t>(right / COLLISION_CELL_SIZE);
        const auto firstY = static_cast<uint32_t>(top / COLLISION_CELL_SIZE);
        const auto lastY = static_cast<uint32_t>(bottom / COLLISION_CELL_SIZE);
        for (uint32_t x = firstX; x <= lastX; ++x) {
            for (uint32_t y = firstY; y <= lastY; ++y) {
                callback(CollisionCellKey(x, y));
            }
        }
    }

    const std::vector<CfgNodeLayout> &nodes_;
    std::unordered_map<uint64_t, std::vector<BasicBlockId>> cells_;
};

bool PathAvoidsOtherNodes(const std::vector<CfgPoint> &path, BasicBlockId owner, const NodeSpatialIndex &nodes)
{
    for (size_t point = 1U; point < path.size(); ++point) {
        if (nodes.SegmentCrossesOtherNode(path[point - 1U], path[point], owner)) {
            return false;
        }
    }
    return true;
}

std::vector<CfgPoint> NormalRoute(const CfgNodeLayout &source, const CfgNodeLayout &target)
{
    const CfgPoint start {source.x + source.width / 2.0, source.y + source.height};
    const CfgPoint end {target.x + target.width / 2.0, target.y};
    std::vector<CfgPoint> points {start};
    if (start.x != end.x) {
        const double middleY = (start.y + end.y) / 2.0;
        points.push_back({start.x, middleY});
        points.push_back({end.x, middleY});
    }
    points.push_back(end);
    return points;
}

std::optional<std::vector<CfgPoint>> FindTerminalRoute(const CfgNodeLayout &source, const NodeSpatialIndex &nodes)
{
    const double centerX = source.x + source.width / 2.0;
    const double centerY = source.y + source.height / 2.0;
    const double right = source.x + source.width;
    const double bottom = source.y + source.height;
    const std::vector<std::vector<CfgPoint>> candidates {
        {{centerX, bottom}, {centerX, bottom + TERMINAL_EDGE_LENGTH}},
        {{right, centerY}, {right + TERMINAL_EDGE_LENGTH, centerY}},
        {{source.x, centerY}, {std::max(0.0, source.x - TERMINAL_EDGE_LENGTH), centerY}},
    };
    const auto found = std::find_if(candidates.begin(), candidates.end(), [&source, &nodes](const auto &candidate) {
        return PathAvoidsOtherNodes(candidate, source.block, nodes);
    });
    return found == candidates.end() ? std::nullopt : std::optional<std::vector<CfgPoint>> {*found};
}

std::optional<std::vector<CfgPoint>> FindOuterLaneConnection(const CfgNodeLayout &node, double laneX,
                                                             const NodeSpatialIndex &nodes, double sidePortRatio)
{
    const double horizontalPortX = node.x + node.width * sidePortRatio;
    const double sidePortY = node.y + node.height * sidePortRatio;
    const double right = node.x + node.width;
    const double bottom = node.y + node.height;
    // Retain separate ports/tracks when a pinned obstacle forces a top/bottom escape.
    // Stay within OUTER_LANE_GAP so lane interval reservations still cover these escapes.
    const double trackRatio = 0.5 + 0.5 * sidePortRatio;
    const double trackGap = OUTER_LANE_GAP * trackRatio;
    const double topTrack = node.y - std::min(OUTER_LANE_GAP, node.y) * trackRatio;
    const double boundaryTrack = std::max(0.0, node.y - OUTER_LANE_GAP);
    const std::vector<std::vector<CfgPoint>> candidates {
        {{right, sidePortY}, {laneX, sidePortY}},
        {{horizontalPortX, bottom}, {horizontalPortX, bottom + trackGap}, {laneX, bottom + trackGap}},
        {{horizontalPortX, node.y}, {horizontalPortX, topTrack}, {laneX, topTrack}},
        // Preserve the boundary escape if pinned obstacles leave no room for a separate top track.
        {{horizontalPortX, node.y}, {horizontalPortX, boundaryTrack}, {laneX, boundaryTrack}},
    };
    const auto found = std::find_if(candidates.begin(), candidates.end(), [&node, &nodes](const auto &candidate) {
        return PathAvoidsOtherNodes(candidate, node.block, nodes);
    });
    if (found == candidates.end()) {
        return std::nullopt;
    }
    return *found;
}

// Geometry of one outer-lane routing request: the lane's x position and both port ratios.
struct OuterRouteGeometry {
    double laneX {0.0};
    double sourcePortRatio {0.0};
    double targetPortRatio {0.0};
};

// Appends the target connection in reverse order, skipping a duplicated junction point.
void AppendReversedConnection(std::vector<CfgPoint> *route, const std::vector<CfgPoint> &connection)
{
    for (auto point = connection.rbegin(); point != connection.rend(); ++point) {
        if (route->back().x != point->x || route->back().y != point->y) {
            route->push_back(*point);
        }
    }
}

std::optional<std::vector<CfgPoint>> FindOuterRoute(const CfgNodeLayout &source, const CfgNodeLayout &target,
                                                    const OuterRouteGeometry &outerRoute, const NodeSpatialIndex &nodes)
{
    auto sourceConnection = FindOuterLaneConnection(source, outerRoute.laneX, nodes, outerRoute.sourcePortRatio);
    const auto targetConnection = FindOuterLaneConnection(target, outerRoute.laneX, nodes, outerRoute.targetPortRatio);
    if (!sourceConnection.has_value() || !targetConnection.has_value()) {
        return std::nullopt;
    }
    AppendReversedConnection(&*sourceConnection, *targetConnection);
    // Local lanes can pass between columns: validate the connecting vertical segment as well as both ports.
    if (!PathAvoidsOtherNodes(*sourceConnection, source.block, nodes)) {
        return std::nullopt;
    }
    return sourceConnection;
}

// Working state shared by the ordered CfgLayoutEngine::Layout pipeline phases below.
// Phases fill their own slices in order and only read earlier slices, so the pipeline
// keeps the exact ordering of the original single-pass implementation.
struct CfgLayoutWorkspace {
    std::vector<NodeGeometry> geometry;
    std::vector<std::vector<BasicBlockId>> successors;
    std::vector<uint32_t> ranks;
    std::vector<std::vector<BasicBlockId>> rows;
    std::vector<double> normalRowWidths;
    std::vector<double> handlerRowWidths;
    std::vector<double> rowHeights;
    double widestNormalRow {0.0};
    double widestHandlerRow {0.0};
    std::vector<CfgNodeLayout> placed;
    std::unique_ptr<NodeSpatialIndex> placedIndex;
    std::vector<std::pair<BasicBlockId, BasicBlockId>> naturalBackEdges;
    std::vector<uint8_t> backEdges;
    std::vector<uint8_t> outerRouting;
    std::vector<OuterPortSlots> outerPorts;
    std::vector<OuterEdgeInterval> outerEdges;
    std::vector<uint32_t> edgeLanes;
    double contentRight {0.0};
    double contentBottom {0.0};
};

// A requested node override is invalid when it duplicates another request for the same
// block, leaves the allowed size range, or pins geometry outside the canvas.
bool IsRequestedNodeInvalid(const CfgNodeGeometry &node, size_t blockCount, const std::vector<uint8_t> &supplied,
                            const CfgLayoutLimits &limits)
{
    if (node.block >= blockCount || supplied[node.block] != 0U ||
        !IsWithin(node.width, MIN_NODE_WIDTH, MAX_NODE_WIDTH) ||
        !IsWithin(node.height, MIN_NODE_HEIGHT, MAX_NODE_HEIGHT)) {
        return true;
    }
    if (!node.pinnedPosition.has_value()) {
        return false;
    }
    const auto &pinned = *node.pinnedPosition;
    return !IsFinitePoint(pinned) || pinned.x < 0.0 || pinned.y < 0.0 || node.width > limits.maxCanvasExtent ||
           node.height > limits.maxCanvasExtent || pinned.x > limits.maxCanvasExtent - node.width ||
           pinned.y > limits.maxCanvasExtent - node.height;
}

// Phase 1: default node geometry from the block contents, then request overrides.
CfgLayoutResult ApplyNodeGeometry(const ControlFlowGraph &graph, const CfgLayoutRequest &request,
                                  const CfgLayoutLimits &limits, CfgLayoutWorkspace *work)
{
    const size_t blockCount = graph.blocks.size();
    work->geometry.resize(blockCount);
    for (size_t block = 0U; block < blockCount; ++block) {
        work->geometry[block].width = DefaultNodeWidth(graph, graph.blocks[block]);
        work->geometry[block].height = DefaultNodeHeight(graph.blocks[block]);
    }
    std::vector<uint8_t> supplied(blockCount, 0U);
    for (const auto &node : request.nodes) {
        if (IsRequestedNodeInvalid(node, blockCount, supplied, limits)) {
            return Failure(CfgLayoutStatus::INVALID_REQUEST,
                           "CFG node geometry is duplicate, out of range, or non-finite");
        }
        supplied[node.block] = 1U;
        work->geometry[node.block] = {node.width, node.height, node.pinnedPosition};
    }
    return {CfgLayoutStatus::SUCCESS, nullptr, {}};
}

// Phase 2: BFS ranks from the entry block plus a longest-path fixup for forward edges.
CfgLayoutResult RankBlocks(const CfgAnalysis &analysis, const ControlFlowGraph &graph,
                           const CancellationToken &cancellation, CfgLayoutWorkspace *work)
{
    const size_t blockCount = graph.blocks.size();
    work->successors.assign(blockCount, {});
    for (const auto &edge : graph.edges) {
        if (edge.target.has_value()) {
            work->successors[edge.source].push_back(*edge.target);
        }
    }
    work->ranks.assign(blockCount, std::numeric_limits<uint32_t>::max());
    std::vector<BasicBlockId> queue;
    queue.reserve(blockCount);
    work->ranks[graph.entryBlock] = 0U;
    queue.push_back(graph.entryBlock);
    for (size_t cursor = 0U; cursor < queue.size(); ++cursor) {
        const auto block = queue[cursor];
        for (const auto target : work->successors[block]) {
            if (!analysis.blockInfo[target].reachable || work->ranks[target] != std::numeric_limits<uint32_t>::max()) {
                continue;
            }
            work->ranks[target] = work->ranks[block] + 1U;
            queue.push_back(target);
        }
        if (cancellation.IsCancellationRequested()) {
            return Failure(CfgLayoutStatus::CANCELLED, "CFG layout was cancelled");
        }
    }
    for (BasicBlockId source = 0U; source < blockCount; ++source) {
        if (!analysis.blockInfo[source].reachable) {
            continue;
        }
        if (work->ranks[source] == std::numeric_limits<uint32_t>::max()) {
            return Failure(CfgLayoutStatus::INVALID_ANALYSIS, "Reachable CFG block has no layout rank");
        }
        for (const auto target : work->successors[source]) {
            if (target > source && analysis.blockInfo[target].reachable) {
                work->ranks[target] = std::max(work->ranks[target], work->ranks[source] + 1U);
            }
        }
    }
    return {CfgLayoutStatus::SUCCESS, nullptr, {}};
}

// Phase 3: moves unreachable blocks into overflow rows after the deepest reachable rank.
uint32_t DemoteUnreachableBlocks(const CfgAnalysis &analysis, size_t blockCount, std::vector<uint32_t> *ranks)
{
    uint32_t maxRank = 0U;
    for (size_t block = 0U; block < blockCount; ++block) {
        if (analysis.blockInfo[block].reachable) {
            maxRank = std::max(maxRank, (*ranks)[block]);
        }
    }
    uint32_t unreachableIndex = 0U;
    for (size_t block = 0U; block < blockCount; ++block) {
        if (!analysis.blockInfo[block].reachable) {
            (*ranks)[block] = maxRank + 1U + unreachableIndex / 4U;
            ++unreachableIndex;
        }
    }
    if (unreachableIndex != 0U) {
        maxRank += 1U + (unreachableIndex - 1U) / 4U;
    }
    return maxRank;
}

// Phase 4: groups blocks into rank rows and measures the per-row widths and heights.
void BuildRowMetrics(const ControlFlowGraph &graph, uint32_t maxRank, CfgLayoutWorkspace *work)
{
    work->rows.assign(static_cast<size_t>(maxRank) + 1U, {});
    for (BasicBlockId block = 0U; block < graph.blocks.size(); ++block) {
        work->rows[work->ranks[block]].push_back(block);
    }
    work->normalRowWidths.assign(work->rows.size(), 0.0);
    work->handlerRowWidths.assign(work->rows.size(), 0.0);
    work->rowHeights.assign(work->rows.size(), 0.0);
    for (size_t rank = 0U; rank < work->rows.size(); ++rank) {
        auto &row = work->rows[rank];
        std::sort(row.begin(), row.end(), [&graph](BasicBlockId left, BasicBlockId right) {
            if (graph.blocks[left].isHandler != graph.blocks[right].isHandler) {
                return !graph.blocks[left].isHandler;
            }
            return left < right;
        });
        size_t normalCount = 0U;
        size_t handlerCount = 0U;
        for (const auto block : row) {
            auto &rowWidth = graph.blocks[block].isHandler ? work->handlerRowWidths[rank] : work->normalRowWidths[rank];
            auto &groupCount = graph.blocks[block].isHandler ? handlerCount : normalCount;
            if (groupCount++ != 0U) {
                rowWidth += HORIZONTAL_GAP;
            }
            rowWidth += work->geometry[block].width;
            work->rowHeights[rank] = std::max(work->rowHeights[rank], work->geometry[block].height);
        }
        work->widestNormalRow = std::max(work->widestNormalRow, work->normalRowWidths[rank]);
        work->widestHandlerRow = std::max(work->widestHandlerRow, work->handlerRowWidths[rank]);
    }
}

// Phase 5: positions every block within its row and records the content bounds.
void PlaceNodes(const ControlFlowGraph &graph, CfgLayoutWorkspace *work)
{
    work->placed.resize(graph.blocks.size());
    double rankY = CANVAS_MARGIN;
    for (size_t rank = 0U; rank < work->rows.size(); ++rank) {
        double normalX = CANVAS_MARGIN + (work->widestNormalRow - work->normalRowWidths[rank]) / 2.0;
        double handlerX = CANVAS_MARGIN + work->widestNormalRow + HANDLER_GAP +
                          (work->widestHandlerRow - work->handlerRowWidths[rank]) / 2.0;
        for (const auto block : work->rows[rank]) {
            auto &nodeX = graph.blocks[block].isHandler ? handlerX : normalX;
            const auto &geometry = work->geometry[block];
            work->placed[block] = {block,
                                   geometry.pinnedPosition.has_value() ? geometry.pinnedPosition->x : nodeX,
                                   geometry.pinnedPosition.has_value() ? geometry.pinnedPosition->y : rankY,
                                   geometry.width,
                                   geometry.height,
                                   static_cast<uint32_t>(rank),
                                   geometry.pinnedPosition.has_value()};
            nodeX += geometry.width + HORIZONTAL_GAP;
        }
        rankY += work->rowHeights[rank] + VERTICAL_GAP;
    }
    for (const auto &node : work->placed) {
        work->contentRight = std::max(work->contentRight, node.x + node.width);
        work->contentBottom = std::max(work->contentBottom, node.y + node.height);
    }
}

// Phase 6: sorts the natural back edges and decides which edges need outer-lane routing.
void ClassifyEdgeRouting(const ControlFlowGraph &graph, const CfgAnalysis &analysis, CfgLayoutWorkspace *work)
{
    for (const auto &loop : analysis.naturalLoops) {
        for (const auto source : loop.backEdgeSources) {
            work->naturalBackEdges.emplace_back(source, loop.header);
        }
    }
    std::sort(work->naturalBackEdges.begin(), work->naturalBackEdges.end());
    work->naturalBackEdges.erase(std::unique(work->naturalBackEdges.begin(), work->naturalBackEdges.end()),
                                 work->naturalBackEdges.end());
    work->backEdges.assign(graph.edges.size(), 0U);
    work->outerRouting.assign(graph.edges.size(), 0U);
    work->outerPorts.resize(graph.blocks.size());
    for (const auto &edge : graph.edges) {
        if (!edge.target.has_value()) {
            continue;
        }
        const bool backEdge = std::binary_search(work->naturalBackEdges.begin(), work->naturalBackEdges.end(),
                                                 std::pair<BasicBlockId, BasicBlockId> {edge.source, *edge.target}) ||
                              work->ranks[*edge.target] <= work->ranks[edge.source];
        const bool skipsRank = work->ranks[*edge.target] > work->ranks[edge.source] &&
                               work->ranks[*edge.target] - work->ranks[edge.source] > 1U;
        work->backEdges[edge.id] = backEdge ? 1U : 0U;
        const auto &source = work->placed[edge.source];
        const auto &target = work->placed[*edge.target];
        const bool coordinatesRequireOuterRoute = target.y < source.y + source.height;
        const bool normalRouteBlocked =
            !PathAvoidsOtherNodes(NormalRoute(source, target), source.block, *work->placedIndex);
        if (backEdge || skipsRank || edge.kind == CfgEdgeKind::EXCEPTION || coordinatesRequireOuterRoute ||
            normalRouteBlocked) {
            work->outerRouting[edge.id] = 1U;
            ++work->outerPorts[edge.source].outgoingCount;
            ++work->outerPorts[*edge.target].incomingCount;
            const double intervalStart = std::max(0.0, std::min(source.y, target.y) - OUTER_LANE_GAP);
            const double intervalEnd = std::max(source.y + source.height, target.y + target.height) + OUTER_LANE_GAP;
            work->outerEdges.push_back({edge.id, intervalStart, intervalEnd});
        }
    }
    std::sort(work->outerEdges.begin(), work->outerEdges.end(), [](const auto &left, const auto &right) {
        return std::tie(left.start, left.end, left.edge) < std::tie(right.start, right.end, right.edge);
    });
}

// Phase 7: interval-schedules outer-lane numbers so overlapping edge intervals never share a lane.
CfgLayoutResult AssignOuterLanes(const CfgLayoutLimits &limits, CfgLayoutWorkspace *work)
{
    using ActiveLane = std::pair<double, uint32_t>;
    std::priority_queue<ActiveLane, std::vector<ActiveLane>, std::greater<>> activeLanes;
    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<>> freeLanes;
    work->edgeLanes.assign(work->outerRouting.size(), std::numeric_limits<uint32_t>::max());
    uint32_t nextLane = 0U;
    for (const auto &interval : work->outerEdges) {
        while (!activeLanes.empty() && activeLanes.top().first + OUTER_LANE_SPACING < interval.start) {
            freeLanes.push(activeLanes.top().second);
            activeLanes.pop();
        }
        uint32_t lane = 0U;
        if (!freeLanes.empty()) {
            lane = freeLanes.top();
            freeLanes.pop();
        } else {
            if (nextLane >= limits.maxOuterLaneCount || nextLane == std::numeric_limits<uint32_t>::max()) {
                return Failure(CfgLayoutStatus::RESOURCE_LIMIT_EXCEEDED,
                               "CFG outer routing lanes exceed the configured limit");
            }
            lane = nextLane++;
        }
        work->edgeLanes[interval.edge] = lane;
        activeLanes.push({interval.end, lane});
    }
    return {CfgLayoutStatus::SUCCESS, nullptr, {}};
}

// Routes one terminal (return/throw) edge into free space around its source node.
CfgLayoutResult AppendTerminalRoute(const ControlFlowEdge &edge, const CfgLayoutWorkspace &work, CfgEdgeLayout *route,
                                    double *routeRight, double *routeBottom)
{
    const auto &source = work.placed[edge.source];
    const auto terminal = FindTerminalRoute(source, *work.placedIndex);
    if (!terminal.has_value()) {
        return Failure(CfgLayoutStatus::INVALID_REQUEST,
                       "CFG pinned geometry leaves no unobstructed terminal edge route");
    }
    route->points.insert(route->points.end(), terminal->begin(), terminal->end());
    for (const auto &point : route->points) {
        *routeRight = std::max(*routeRight, point.x);
        *routeBottom = std::max(*routeBottom, point.y);
    }
    return {CfgLayoutStatus::SUCCESS, nullptr, {}};
}

// Routes one edge through a reserved outer lane, preferring a local lane for forward skips.
CfgLayoutResult AppendOuterRoute(const ControlFlowEdge &edge, CfgLayoutWorkspace *work, CfgEdgeLayout *route,
                                 double *routeRight, double *routeBottom)
{
    const auto &source = work->placed[edge.source];
    const auto &target = work->placed[*edge.target];
    auto &sourcePorts = work->outerPorts[edge.source];
    auto &targetPorts = work->outerPorts[*edge.target];
    const double sourcePortRatio = OuterPortRatio(sourcePorts.nextOutgoing++, sourcePorts.outgoingCount, true);
    const double targetPortRatio = OuterPortRatio(targetPorts.nextIncoming++, targetPorts.incomingCount, false);
    const uint32_t lane = work->edgeLanes[edge.id];
    const double laneGap = OUTER_LANE_GAP + static_cast<double>(lane) * OUTER_LANE_SPACING;
    double laneX = work->contentRight + laneGap;
    std::optional<std::vector<CfgPoint>> points;
    const bool forwardSkip = !route->isBackEdge && !route->isExceptionEdge && target.y >= source.y + source.height;
    if (forwardSkip) {
        // A forward skip need not wrap around unrelated handlers in the far-right column.
        const double localLaneX = std::max(source.x + source.width, target.x + target.width) + laneGap;
        const OuterRouteGeometry localRoute {localLaneX, sourcePortRatio, targetPortRatio};
        points = FindOuterRoute(source, target, localRoute, *work->placedIndex);
        laneX = points.has_value() ? localLaneX : laneX;
    }
    if (!points.has_value()) {
        const OuterRouteGeometry fallbackRoute {laneX, sourcePortRatio, targetPortRatio};
        points = FindOuterRoute(source, target, fallbackRoute, *work->placedIndex);
    }
    if (!points.has_value()) {
        return Failure(CfgLayoutStatus::INVALID_REQUEST, "CFG pinned geometry leaves no unobstructed outer edge route");
    }
    route->outerLane = lane;
    route->points.insert(route->points.end(), points->begin(), points->end());
    *routeRight = std::max(*routeRight, laneX);
    for (const auto &point : route->points) {
        *routeBottom = std::max(*routeBottom, point.y);
    }
    return {CfgLayoutStatus::SUCCESS, nullptr, {}};
}

// Phase 8: allocates the bounded layout model and fills the per-edge routes.
CfgLayoutResult BuildLayoutModel(const CfgAnalysisPtr &analysis, const CfgLayoutRequest &request,
                                 const CfgLayoutLimits &limits, const CancellationToken &cancellation,
                                 CfgLayoutWorkspace *work)
{
    const auto &graph = *analysis->graph;
    auto resource = std::make_shared<BoundedMemoryResource>(limits.maxModelBytes - sizeof(CfgLayout));
    auto layout = std::make_shared<CfgLayout>(resource, analysis);
    layout->revision = request.revision;
    layout->nodes.assign(work->placed.begin(), work->placed.end());
    layout->edges.reserve(graph.edges.size());
    uint64_t routePointCount = 0U;
    double routeRight = work->contentRight;
    double routeBottom = work->contentBottom;
    for (const auto &edge : graph.edges) {
        if (cancellation.IsCancellationRequested()) {
            return Failure(CfgLayoutStatus::CANCELLED, "CFG layout was cancelled");
        }
        layout->edges.emplace_back(resource.get());
        auto &route = layout->edges.back();
        route.edge = edge.id;
        route.kind = edge.kind;
        route.isBackEdge = work->backEdges[edge.id] != 0U;
        route.isExceptionEdge = edge.kind == CfgEdgeKind::EXCEPTION;
        const auto &source = work->placed[edge.source];
        if (!edge.target.has_value()) {
            const auto terminal = AppendTerminalRoute(edge, *work, &route, &routeRight, &routeBottom);
            if (terminal.status != CfgLayoutStatus::SUCCESS) {
                return terminal;
            }
        } else if (work->outerRouting[edge.id] != 0U) {
            const auto outer = AppendOuterRoute(edge, work, &route, &routeRight, &routeBottom);
            if (outer.status != CfgLayoutStatus::SUCCESS) {
                return outer;
            }
        } else {
            const auto &target = work->placed[*edge.target];
            const auto points = NormalRoute(source, target);
            route.points.insert(route.points.end(), points.begin(), points.end());
        }
        routePointCount += route.points.size();
        if (routePointCount > limits.maxRoutePointCount) {
            return Failure(CfgLayoutStatus::RESOURCE_LIMIT_EXCEEDED,
                           "CFG route point count exceeds the configured limit");
        }
    }
    layout->canvasWidth = routeRight + CANVAS_MARGIN;
    layout->canvasHeight = routeBottom + CANVAS_MARGIN;
    if (layout->canvasWidth > limits.maxCanvasExtent || layout->canvasHeight > limits.maxCanvasExtent) {
        return Failure(CfgLayoutStatus::RESOURCE_LIMIT_EXCEEDED, "CFG canvas extent exceeds the configured limit");
    }
    return {CfgLayoutStatus::SUCCESS, std::move(layout), {}};
}

}  // namespace

CfgLayoutResult CfgLayoutEngine::Layout(CfgAnalysisPtr analysis, const CfgLayoutRequest &request,
                                        const CfgLayoutLimits &limits, const CancellationToken &cancellation) const
{
    if (analysis == nullptr || analysis->graph == nullptr ||
        analysis->blockInfo.size() != analysis->graph->blocks.size()) {
        return Failure(CfgLayoutStatus::INVALID_ANALYSIS, "CFG layout requires a complete graph analysis");
    }
    const auto &graph = *analysis->graph;
    const size_t blockCount = graph.blocks.size();
    if (blockCount > limits.maxNodeCount || graph.edges.size() > limits.maxEdgeCount) {
        return Failure(CfgLayoutStatus::RESOURCE_LIMIT_EXCEEDED,
                       "CFG layout node or edge count exceeds the configured limit");
    }
    if (limits.maxModelBytes < sizeof(CfgLayout) || !std::isfinite(limits.maxCanvasExtent) ||
        limits.maxCanvasExtent <= 0.0) {
        return Failure(CfgLayoutStatus::RESOURCE_LIMIT_EXCEEDED, "CFG layout limits are invalid or too small");
    }

    try {
        CfgLayoutWorkspace work;
        const auto geometry = ApplyNodeGeometry(graph, request, limits, &work);
        if (geometry.status != CfgLayoutStatus::SUCCESS) {
            return geometry;
        }
        const auto ranks = RankBlocks(*analysis, graph, cancellation, &work);
        if (ranks.status != CfgLayoutStatus::SUCCESS) {
            return ranks;
        }
        const auto maxRank = DemoteUnreachableBlocks(*analysis, blockCount, &work.ranks);
        BuildRowMetrics(graph, maxRank, &work);
        PlaceNodes(graph, &work);
        if (HasPinnedOverlap(work.placed)) {
            return Failure(CfgLayoutStatus::INVALID_REQUEST, "CFG pinned node overlaps another placed node");
        }
        work.placedIndex = std::make_unique<NodeSpatialIndex>(work.placed);
        ClassifyEdgeRouting(graph, *analysis, &work);
        const auto lanes = AssignOuterLanes(limits, &work);
        if (lanes.status != CfgLayoutStatus::SUCCESS) {
            return lanes;
        }
        return BuildLayoutModel(analysis, request, limits, cancellation, &work);
    } catch (const ModelBudgetExceeded &) {
        return Failure(CfgLayoutStatus::RESOURCE_LIMIT_EXCEEDED, "CFG layout model exceeds the configured byte limit");
    } catch (const std::bad_alloc &) {
        return Failure(CfgLayoutStatus::RESOURCE_LIMIT_EXCEEDED, "CFG layout ran out of memory");
    }
}

}  // namespace panda::bytecode_explorer
