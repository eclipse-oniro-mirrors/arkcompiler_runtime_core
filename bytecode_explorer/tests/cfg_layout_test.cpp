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

#include "core/cfg/cfg_analysis.h"
#include "core/cfg/cfg_builder.h"
#include "core/cfg/cfg_layout.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer::test {
namespace {

InstructionInfo MakeInstruction(uint32_t pc, ControlFlowKind flowKind,
                                std::optional<uint32_t> jumpTarget = std::nullopt, std::string_view mnemonic = "test",
                                std::string_view operands = {})
{
    InstructionInfo instruction;
    instruction.pc = pc;
    instruction.byteSize = 1U;
    instruction.mnemonic = mnemonic;
    instruction.operands = operands;
    instruction.flowKind = flowKind;
    instruction.jumpTarget = jumpTarget;
    instruction.isTerminator = flowKind != ControlFlowKind::NORMAL;
    return instruction;
}

CfgAnalysisPtr Analyze(std::initializer_list<InstructionInfo> instructions)
{
    auto disassembly = std::make_shared<FunctionDisassembly>();
    disassembly->codeSize = static_cast<uint32_t>(instructions.size());
    for (const auto &instruction : instructions) {
        disassembly->instructions.push_back(instruction);
    }
    const auto built = ControlFlowGraphBuilder {}.Build(disassembly);
    EXPECT_EQ(built.status, CfgStatus::SUCCESS) << built.diagnostic;
    const auto analyzed = CfgAnalyzer {}.Analyze(built.graph);
    EXPECT_EQ(analyzed.status, CfgAnalysisStatus::SUCCESS) << analyzed.diagnostic;
    return analyzed.analysis;
}

const CfgNodeLayout *FindNode(const CfgLayout &layout, BasicBlockId block)
{
    const auto found = std::find_if(layout.nodes.begin(), layout.nodes.end(),
                                    [block](const CfgNodeLayout &node) { return node.block == block; });
    return found == layout.nodes.end() ? nullptr : &*found;
}

TEST(CfgLayoutEngineTest, AdaptsDefaultNodeGeometryToInstructionRowsAndText)
{
    const auto compact = Analyze({MakeInstruction(0U, ControlFlowKind::RETURN, std::nullopt, "return")});
    ASSERT_NE(compact, nullptr);
    const auto compactResult = CfgLayoutEngine {}.Layout(compact);
    ASSERT_EQ(compactResult.status, CfgLayoutStatus::SUCCESS) << compactResult.diagnostic;
    ASSERT_NE(compactResult.layout, nullptr);
    const auto *compactNode = FindNode(*compactResult.layout, 0U);
    ASSERT_NE(compactNode, nullptr);
    EXPECT_DOUBLE_EQ(compactNode->width, 360.0);
    EXPECT_DOUBLE_EQ(compactNode->height, 110.0);

    const auto wide = Analyze(
        {MakeInstruction(0U, ControlFlowKind::RETURN, std::nullopt, "an_instruction_mnemonic_that_needs_more_room",
                         "an_operand_value_that_is_deliberately_long_enough_to_reach_the_basic_block_width_cap")});
    ASSERT_NE(wide, nullptr);
    const auto wideResult = CfgLayoutEngine {}.Layout(wide);
    ASSERT_EQ(wideResult.status, CfgLayoutStatus::SUCCESS) << wideResult.diagnostic;
    ASSERT_NE(wideResult.layout, nullptr);
    const auto *wideNode = FindNode(*wideResult.layout, 0U);
    ASSERT_NE(wideNode, nullptr);
    EXPECT_DOUBLE_EQ(wideNode->width, 640.0);

    const auto longBlock = Analyze({
        MakeInstruction(0U, ControlFlowKind::NORMAL),
        MakeInstruction(1U, ControlFlowKind::NORMAL),
        MakeInstruction(2U, ControlFlowKind::NORMAL),
        MakeInstruction(3U, ControlFlowKind::NORMAL),
        MakeInstruction(4U, ControlFlowKind::NORMAL),
        MakeInstruction(5U, ControlFlowKind::NORMAL),
        MakeInstruction(6U, ControlFlowKind::NORMAL),
        MakeInstruction(7U, ControlFlowKind::NORMAL),
        MakeInstruction(8U, ControlFlowKind::NORMAL),
        MakeInstruction(9U, ControlFlowKind::NORMAL),
        MakeInstruction(10U, ControlFlowKind::NORMAL),
        MakeInstruction(11U, ControlFlowKind::NORMAL),
        MakeInstruction(12U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(longBlock, nullptr);
    const auto longResult = CfgLayoutEngine {}.Layout(longBlock);
    ASSERT_EQ(longResult.status, CfgLayoutStatus::SUCCESS) << longResult.diagnostic;
    ASSERT_NE(longResult.layout, nullptr);
    const auto *longNode = FindNode(*longResult.layout, 0U);
    ASSERT_NE(longNode, nullptr);
    EXPECT_DOUBLE_EQ(longNode->height, 452.0);
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

void ExpectRoutesAvoidNodeInteriors(const CfgLayout &layout)
{
    for (const auto &edge : layout.edges) {
        for (size_t point = 1U; point < edge.points.size(); ++point) {
            for (const auto &node : layout.nodes) {
                EXPECT_FALSE(SegmentCrossesNodeInterior(edge.points[point - 1U], edge.points[point], node))
                    << "edge " << edge.edge << " crosses BB" << node.block;
            }
        }
    }
}

bool CollinearSegmentsOverlap(const CfgPoint &a, const CfgPoint &b, const CfgPoint &c, const CfgPoint &d)
{
    if (a.y == b.y && c.y == d.y && a.y == c.y) {
        return std::max(std::min(a.x, b.x), std::min(c.x, d.x)) <
               std::min(std::max(a.x, b.x), std::max(c.x, d.x));
    }
    if (a.x == b.x && c.x == d.x && a.x == c.x) {
        return std::max(std::min(a.y, b.y), std::min(c.y, d.y)) <
               std::min(std::max(a.y, b.y), std::max(c.y, d.y));
    }
    return false;
}

void ExpectDistinctOuterPorts(const CfgLayout &layout, const ControlFlowGraph &graph)
{
    for (const auto &node : layout.nodes) {
        std::vector<std::pair<CfgPoint, CfgPoint>> stems;
        for (const auto &route : layout.edges) {
            if (!route.outerLane.has_value()) {
                continue;
            }
            ASSERT_GE(route.points.size(), 2U);
            const auto &edge = graph.edges[route.edge];
            if (edge.source == node.block) {
                stems.emplace_back(route.points.front(), route.points[1U]);
            }
            if (edge.target == node.block) {
                stems.emplace_back(route.points.back(), route.points[route.points.size() - 2U]);
            }
        }
        for (size_t i = 0U; i < stems.size(); ++i) {
            for (size_t j = i + 1U; j < stems.size(); ++j) {
                EXPECT_FALSE(stems[i].first.x == stems[j].first.x && stems[i].first.y == stems[j].first.y)
                    << "shared outer port at BB" << node.block;
                EXPECT_FALSE(CollinearSegmentsOverlap(stems[i].first, stems[i].second, stems[j].first, stems[j].second))
                    << "overlapping outer stems at BB" << node.block;
            }
        }
    }
}

// One ordered pair of outer-lane routes: the feeding edge must end above where the fed
// edge starts, and no collinear segments of the two routes may overlap.
void ExpectEdgePairIsSeparated(const ControlFlowGraph &graph, const CfgEdgeLayout &left, const CfgEdgeLayout &right)
{
    if (left.edge >= right.edge || !left.outerLane || !right.outerLane) {
        return;
    }
    if (graph.edges[left.edge].target == graph.edges[right.edge].source) {
        EXPECT_LT(left.points.back().y, right.points.front().y);
    }
    for (size_t i = 1U; i < left.points.size(); ++i) {
        for (size_t j = 1U; j < right.points.size(); ++j) {
            EXPECT_FALSE(
                CollinearSegmentsOverlap(left.points[i - 1U], left.points[i], right.points[j - 1U], right.points[j]));
        }
    }
}

// Every ordered pair of outer-lane routes must stay separated in the forward skip chain.
void ExpectForwardSkipChainRoutesSeparated(const CfgLayout &layout, const ControlFlowGraph &graph)
{
    for (const auto &left : layout.edges) {
        for (const auto &right : layout.edges) {
            ExpectEdgePairIsSeparated(graph, left, right);
        }
    }
}

TEST(CfgLayoutEngineTest, SeparatesIncomingAndOutgoingPortsInForwardSkipChain)
{
    const auto analysis = Analyze({
        MakeInstruction(0U, ControlFlowKind::CONDITIONAL_JUMP, 2U),
        MakeInstruction(1U, ControlFlowKind::NORMAL),
        MakeInstruction(2U, ControlFlowKind::CONDITIONAL_JUMP, 4U),
        MakeInstruction(3U, ControlFlowKind::NORMAL),
        MakeInstruction(4U, ControlFlowKind::CONDITIONAL_JUMP, 6U),
        MakeInstruction(5U, ControlFlowKind::NORMAL),
        MakeInstruction(6U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(analysis, nullptr);
    for (double height : {110.0, 452.0, 3700.0}) {
        CfgLayoutRequest request;
        request.nodes.push_back({2U, 360.0, height, std::nullopt});
        request.nodes.push_back({4U, 500.0, height, std::nullopt});
        const auto result = CfgLayoutEngine {}.Layout(analysis, request);
        ASSERT_EQ(result.status, CfgLayoutStatus::SUCCESS) << result.diagnostic;
        ASSERT_EQ(std::count_if(result.layout->edges.begin(), result.layout->edges.end(),
                               [](const auto &route) { return route.outerLane.has_value(); }), 3);
        ExpectDistinctOuterPorts(*result.layout, *analysis->graph);
        ExpectRoutesAvoidNodeInteriors(*result.layout);
        ExpectForwardSkipChainRoutesSeparated(*result.layout, *analysis->graph);
    }
}

TEST(CfgLayoutEngineTest, SeparatesMultipleIncomingOuterPorts)
{
    const auto analysis = Analyze({
        MakeInstruction(0U, ControlFlowKind::CONDITIONAL_JUMP, 3U),
        MakeInstruction(1U, ControlFlowKind::CONDITIONAL_JUMP, 3U),
        MakeInstruction(2U, ControlFlowKind::NORMAL),
        MakeInstruction(3U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(analysis, nullptr);
    const auto result = CfgLayoutEngine {}.Layout(analysis);
    ASSERT_EQ(result.status, CfgLayoutStatus::SUCCESS) << result.diagnostic;
    ASSERT_EQ(std::count_if(result.layout->edges.begin(), result.layout->edges.end(),
                           [](const auto &route) { return route.outerLane.has_value(); }), 2);
    ExpectDistinctOuterPorts(*result.layout, *analysis->graph);
    ExpectRoutesAvoidNodeInteriors(*result.layout);
}

TEST(CfgLayoutEngineTest, SeparatesMultipleOutgoingOuterPortsForPinnedTargets)
{
    const auto analysis = Analyze({
        MakeInstruction(0U, ControlFlowKind::CONDITIONAL_JUMP, 2U),
        MakeInstruction(1U, ControlFlowKind::RETURN),
        MakeInstruction(2U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(analysis, nullptr);
    CfgLayoutRequest request;
    request.nodes.push_back({1U, 360.0, 110.0, CfgPoint {800.0, 0.0}});
    request.nodes.push_back({2U, 360.0, 110.0, CfgPoint {1300.0, 0.0}});
    const auto result = CfgLayoutEngine {}.Layout(analysis, request);
    ASSERT_EQ(result.status, CfgLayoutStatus::SUCCESS) << result.diagnostic;
    ASSERT_EQ(std::count_if(result.layout->edges.begin(), result.layout->edges.end(),
                           [](const auto &route) { return route.outerLane.has_value(); }), 2);
    ExpectDistinctOuterPorts(*result.layout, *analysis->graph);
    ExpectRoutesAvoidNodeInteriors(*result.layout);
}

TEST(CfgLayoutEngineTest, SeparatesFallbackPortsWhenSideConnectionsAreBlocked)
{
    const auto analysis = Analyze({
        MakeInstruction(0U, ControlFlowKind::CONDITIONAL_JUMP, 2U),
        MakeInstruction(1U, ControlFlowKind::RETURN),
        MakeInstruction(2U, ControlFlowKind::RETURN),
        MakeInstruction(3U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(analysis, nullptr);
    CfgLayoutRequest request;
    request.nodes.push_back({0U, 360.0, 110.0, CfgPoint {80.0, 80.0}});
    request.nodes.push_back({1U, 360.0, 110.0, CfgPoint {800.0, 0.0}});
    request.nodes.push_back({2U, 360.0, 110.0, CfgPoint {1300.0, 0.0}});
    request.nodes.push_back({3U, 120.0, 110.0, CfgPoint {480.0, 80.0}});
    const auto result = CfgLayoutEngine {}.Layout(analysis, request);
    ASSERT_EQ(result.status, CfgLayoutStatus::SUCCESS) << result.diagnostic;
    const auto *source = FindNode(*result.layout, 0U);
    ASSERT_NE(source, nullptr);
    size_t fallbackCount = 0U;
    for (const auto &route : result.layout->edges) {
        if (route.outerLane && analysis->graph->edges[route.edge].source == 0U) {
            EXPECT_DOUBLE_EQ(route.points.front().y, source->y + source->height);
            ++fallbackCount;
        }
    }
    ASSERT_EQ(fallbackCount, 2U);
    ExpectDistinctOuterPorts(*result.layout, *analysis->graph);
    ExpectRoutesAvoidNodeInteriors(*result.layout);
}

TEST(CfgLayoutEngineTest, SeparatesTopFallbackTracksNearCanvasBoundary)
{
    const auto analysis = Analyze({
        MakeInstruction(0U, ControlFlowKind::CONDITIONAL_JUMP, 2U),
        MakeInstruction(1U, ControlFlowKind::RETURN),
        MakeInstruction(2U, ControlFlowKind::RETURN),
        MakeInstruction(3U, ControlFlowKind::RETURN),
        MakeInstruction(4U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(analysis, nullptr);
    CfgLayoutRequest request;
    request.nodes.push_back({0U, 360.0, 110.0, CfgPoint {80.0, 24.0}});
    request.nodes.push_back({1U, 360.0, 110.0, CfgPoint {800.0, 300.0}});
    request.nodes.push_back({2U, 360.0, 110.0, CfgPoint {1300.0, 300.0}});
    request.nodes.push_back({3U, 120.0, 110.0, CfgPoint {480.0, 24.0}});
    request.nodes.push_back({4U, 360.0, 60.0, CfgPoint {80.0, 140.0}});
    const auto result = CfgLayoutEngine {}.Layout(analysis, request);
    ASSERT_EQ(result.status, CfgLayoutStatus::SUCCESS) << result.diagnostic;
    std::vector<const CfgEdgeLayout *> outgoing;
    for (const auto &route : result.layout->edges) {
        if (route.outerLane && analysis->graph->edges[route.edge].source == 0U) {
            ASSERT_GE(route.points.size(), 3U);
            EXPECT_DOUBLE_EQ(route.points.front().y, 24.0);
            EXPECT_GE(route.points[1U].y, 0.0);
            EXPECT_LT(route.points[1U].y, 24.0);
            outgoing.push_back(&route);
        }
    }
    ASSERT_EQ(outgoing.size(), 2U);
    EXPECT_NE(outgoing[0]->points[1U].y, outgoing[1]->points[1U].y);
    EXPECT_FALSE(CollinearSegmentsOverlap(outgoing[0]->points[1U], outgoing[0]->points[2U],
                                         outgoing[1]->points[1U], outgoing[1]->points[2U]));
    ExpectDistinctOuterPorts(*result.layout, *analysis->graph);
    ExpectRoutesAvoidNodeInteriors(*result.layout);

    // If targets obstruct every inner top track, retain the previous canvas-boundary escape.
    request.nodes[1U].pinnedPosition->y = 0.0;
    request.nodes[2U].pinnedPosition->y = 0.0;
    const auto constrained = CfgLayoutEngine {}.Layout(analysis, request);
    ASSERT_EQ(constrained.status, CfgLayoutStatus::SUCCESS) << constrained.diagnostic;
    ExpectRoutesAvoidNodeInteriors(*constrained.layout);
}

TEST(CfgLayoutEngineTest, PlacesDiamondInDeterministicTopDownRanksAndRoutesOrthogonally)
{
    const auto analysis = Analyze({
        MakeInstruction(0U, ControlFlowKind::CONDITIONAL_JUMP, 2U),
        MakeInstruction(1U, ControlFlowKind::UNCONDITIONAL_JUMP, 3U),
        MakeInstruction(2U, ControlFlowKind::UNCONDITIONAL_JUMP, 3U),
        MakeInstruction(3U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(analysis, nullptr);

    const auto first = CfgLayoutEngine {}.Layout(analysis);
    const auto second = CfgLayoutEngine {}.Layout(analysis);

    ASSERT_EQ(first.status, CfgLayoutStatus::SUCCESS) << first.diagnostic;
    ASSERT_EQ(second.status, CfgLayoutStatus::SUCCESS) << second.diagnostic;
    ASSERT_NE(first.layout, nullptr);
    ASSERT_NE(second.layout, nullptr);
    ASSERT_EQ(first.layout->nodes.size(), 4U);
    const auto *entry = FindNode(*first.layout, 0U);
    const auto *leftBranch = FindNode(*first.layout, 1U);
    const auto *rightBranch = FindNode(*first.layout, 2U);
    const auto *merge = FindNode(*first.layout, 3U);
    ASSERT_NE(entry, nullptr);
    ASSERT_NE(leftBranch, nullptr);
    ASSERT_NE(rightBranch, nullptr);
    ASSERT_NE(merge, nullptr);
    EXPECT_EQ(entry->rank, 0U);
    EXPECT_EQ(leftBranch->rank, 1U);
    EXPECT_EQ(rightBranch->rank, 1U);
    EXPECT_EQ(merge->rank, 2U);
    EXPECT_LT(entry->y, leftBranch->y);
    EXPECT_LT(leftBranch->y, merge->y);
    EXPECT_NE(leftBranch->x, rightBranch->x);
    EXPECT_DOUBLE_EQ(first.layout->canvasWidth, second.layout->canvasWidth);
    EXPECT_DOUBLE_EQ(first.layout->canvasHeight, second.layout->canvasHeight);

    for (const auto &edge : first.layout->edges) {
        ASSERT_GE(edge.points.size(), 2U);
        for (size_t point = 1U; point < edge.points.size(); ++point) {
            const auto &previous = edge.points[point - 1U];
            const auto &current = edge.points[point];
            EXPECT_TRUE(previous.x == current.x || previous.y == current.y);
        }
    }
    ExpectRoutesAvoidNodeInteriors(*first.layout);
}

TEST(CfgLayoutEngineTest, RoutesBackEdgeThroughAnOuterLane)
{
    const auto analysis = Analyze({
        MakeInstruction(0U, ControlFlowKind::NORMAL),
        MakeInstruction(1U, ControlFlowKind::CONDITIONAL_JUMP, 1U),
        MakeInstruction(2U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(analysis, nullptr);

    const auto result = CfgLayoutEngine {}.Layout(analysis);

    ASSERT_EQ(result.status, CfgLayoutStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.layout, nullptr);
    const auto found = std::find_if(result.layout->edges.begin(), result.layout->edges.end(),
                                    [](const CfgEdgeLayout &edge) { return edge.isBackEdge; });
    ASSERT_NE(found, result.layout->edges.end());
    ASSERT_GE(found->points.size(), 4U);
    const auto maxNodeRight = std::max_element(
        result.layout->nodes.begin(), result.layout->nodes.end(),
        [](const auto &left, const auto &right) { return left.x + left.width < right.x + right.width; });
    ASSERT_NE(maxNodeRight, result.layout->nodes.end());
    EXPECT_GT(found->points[1].x, maxNodeRight->x + maxNodeRight->width);
}

TEST(CfgLayoutEngineTest, RoutesForwardEdgeAroundBlocksInSkippedRanks)
{
    const auto analysis = Analyze({
        MakeInstruction(0U, ControlFlowKind::CONDITIONAL_JUMP, 2U),
        MakeInstruction(1U, ControlFlowKind::NORMAL),
        MakeInstruction(2U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(analysis, nullptr);

    const auto result = CfgLayoutEngine {}.Layout(analysis);

    ASSERT_EQ(result.status, CfgLayoutStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.layout, nullptr);
    const auto found = std::find_if(result.layout->edges.begin(), result.layout->edges.end(),
                                    [](const auto &edge) { return edge.kind == CfgEdgeKind::TRUE_BRANCH; });
    ASSERT_NE(found, result.layout->edges.end());
    ASSERT_TRUE(found->outerLane.has_value());
    ASSERT_GE(found->points.size(), 4U);
    const auto maxNodeRight = std::max_element(
        result.layout->nodes.begin(), result.layout->nodes.end(),
        [](const auto &left, const auto &right) { return left.x + left.width < right.x + right.width; });
    ASSERT_NE(maxNodeRight, result.layout->nodes.end());
    EXPECT_GT(found->points[1].x, maxNodeRight->x + maxNodeRight->width);
}

TEST(CfgLayoutEngineTest, KeepsForwardSkipLaneLocalWhenHandlerColumnIsFarAway)
{
    auto disassembly = std::make_shared<FunctionDisassembly>();
    disassembly->codeSize = 5U;
    disassembly->instructions.push_back(MakeInstruction(0U, ControlFlowKind::CONDITIONAL_JUMP, 2U));
    auto protectedInstruction = MakeInstruction(1U, ControlFlowKind::NORMAL);
    protectedInstruction.canThrow = true;
    disassembly->instructions.push_back(std::move(protectedInstruction));
    disassembly->instructions.push_back(MakeInstruction(2U, ControlFlowKind::NORMAL));
    disassembly->instructions.push_back(MakeInstruction(3U, ControlFlowKind::RETURN));
    disassembly->instructions.push_back(MakeInstruction(4U, ControlFlowKind::THROW));
    disassembly->tryRegions.emplace_back();
    disassembly->tryRegions.back().startPc = 1U;
    disassembly->tryRegions.back().endPc = 2U;
    disassembly->tryRegions.back().handlers.push_back({0U, 4U, 1U, 0U});
    const auto built = ControlFlowGraphBuilder {}.Build(disassembly);
    ASSERT_EQ(built.status, CfgStatus::SUCCESS) << built.diagnostic;
    const auto analyzed = CfgAnalyzer {}.Analyze(built.graph);
    ASSERT_EQ(analyzed.status, CfgAnalysisStatus::SUCCESS) << analyzed.diagnostic;
    const auto result = CfgLayoutEngine {}.Layout(analyzed.analysis);
    ASSERT_EQ(result.status, CfgLayoutStatus::SUCCESS) << result.diagnostic;
    const auto *handler = FindNode(*result.layout, 3U);
    ASSERT_NE(handler, nullptr);
    const auto skip = std::find_if(result.layout->edges.begin(), result.layout->edges.end(), [](const auto &edge) {
        return edge.kind == CfgEdgeKind::TRUE_BRANCH;
    });
    ASSERT_NE(skip, result.layout->edges.end());
    ASSERT_TRUE(skip->outerLane.has_value());
    for (const auto &point : skip->points) {
        EXPECT_LT(point.x, handler->x);
    }
    ExpectRoutesAvoidNodeInteriors(*result.layout);
}

TEST(CfgLayoutEngineTest, FallsBackWhenPinnedNodeObstructsLocalLaneVerticalSegment)
{
    const auto analysis = Analyze({
        MakeInstruction(0U, ControlFlowKind::CONDITIONAL_JUMP, 2U),
        MakeInstruction(1U, ControlFlowKind::NORMAL),
        MakeInstruction(2U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(analysis, nullptr);
    CfgLayoutRequest request;
    request.nodes.push_back({1U, 360.0, 110.0, CfgPoint {450.0, 320.0}});
    const auto result = CfgLayoutEngine {}.Layout(analysis, request);
    ASSERT_EQ(result.status, CfgLayoutStatus::SUCCESS) << result.diagnostic;
    const auto *obstacle = FindNode(*result.layout, 1U);
    ASSERT_NE(obstacle, nullptr);
    const auto skip = std::find_if(result.layout->edges.begin(), result.layout->edges.end(), [](const auto &edge) {
        return edge.kind == CfgEdgeKind::TRUE_BRANCH;
    });
    ASSERT_NE(skip, result.layout->edges.end());
    ASSERT_GE(skip->points.size(), 4U);
    EXPECT_GT(skip->points[1].x, obstacle->x + obstacle->width);
    ExpectRoutesAvoidNodeInteriors(*result.layout);
}

TEST(CfgLayoutEngineTest, PreservesValidatedPinnedPositionAndMeasuredNodeSize)
{
    const auto analysis = Analyze({
        MakeInstruction(0U, ControlFlowKind::CONDITIONAL_JUMP, 2U),
        MakeInstruction(1U, ControlFlowKind::RETURN),
        MakeInstruction(2U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(analysis, nullptr);
    CfgLayoutRequest request;
    request.revision = 7U;
    request.nodes.push_back({2U, 500.0, 220.0, CfgPoint {1200.0, 80.0}});

    const auto result = CfgLayoutEngine {}.Layout(analysis, request);

    ASSERT_EQ(result.status, CfgLayoutStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.layout, nullptr);
    EXPECT_EQ(result.layout->revision, 7U);
    const auto *pinned = FindNode(*result.layout, 2U);
    ASSERT_NE(pinned, nullptr);
    EXPECT_TRUE(pinned->pinned);
    EXPECT_DOUBLE_EQ(pinned->x, 1200.0);
    EXPECT_DOUBLE_EQ(pinned->y, 80.0);
    EXPECT_DOUBLE_EQ(pinned->width, 500.0);
    EXPECT_DOUBLE_EQ(pinned->height, 220.0);
}

TEST(CfgLayoutEngineTest, RejectsPinnedNodeOverlappingAnotherPlacedNode)
{
    const auto analysis = Analyze({
        MakeInstruction(0U, ControlFlowKind::CONDITIONAL_JUMP, 2U),
        MakeInstruction(1U, ControlFlowKind::RETURN),
        MakeInstruction(2U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(analysis, nullptr);
    const auto initial = CfgLayoutEngine {}.Layout(analysis);
    ASSERT_EQ(initial.status, CfgLayoutStatus::SUCCESS) << initial.diagnostic;
    ASSERT_NE(initial.layout, nullptr);
    const auto *occupied = FindNode(*initial.layout, 2U);
    ASSERT_NE(occupied, nullptr);
    CfgLayoutRequest request;
    request.nodes.push_back({1U, occupied->width, occupied->height, CfgPoint {occupied->x, occupied->y}});

    const auto result = CfgLayoutEngine {}.Layout(analysis, request);

    EXPECT_EQ(result.status, CfgLayoutStatus::INVALID_REQUEST);
    EXPECT_EQ(result.layout, nullptr);
    EXPECT_NE(result.diagnostic.find("overlap"), std::string::npos);
}

TEST(CfgLayoutEngineTest, RoutesToPinnedTargetAboveSourceWithoutCrossingNodeInteriors)
{
    const auto analysis = Analyze({
        MakeInstruction(0U, ControlFlowKind::UNCONDITIONAL_JUMP, 1U),
        MakeInstruction(1U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(analysis, nullptr);
    CfgLayoutRequest request;
    request.nodes.push_back({1U, 420.0, 110.0, CfgPoint {900.0, 0.0}});

    const auto result = CfgLayoutEngine {}.Layout(analysis, request);

    ASSERT_EQ(result.status, CfgLayoutStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.layout, nullptr);
    const auto routed = std::find_if(result.layout->edges.begin(), result.layout->edges.end(),
                                     [](const auto &edge) { return edge.kind != CfgEdgeKind::RETURN; });
    ASSERT_NE(routed, result.layout->edges.end());
    EXPECT_TRUE(routed->outerLane.has_value());
    ExpectRoutesAvoidNodeInteriors(*result.layout);
}

TEST(CfgLayoutEngineTest, PromotesOrdinaryRouteToOuterLaneWhenPinnedNodeBlocksTheMiddleSegment)
{
    const auto analysis = Analyze({
        MakeInstruction(0U, ControlFlowKind::UNCONDITIONAL_JUMP, 1U),
        MakeInstruction(1U, ControlFlowKind::RETURN),
        MakeInstruction(2U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(analysis, nullptr);
    CfgLayoutRequest request;
    request.nodes.push_back({2U, 120.0, 60.0, CfgPoint {230.0, 225.0}});

    const auto result = CfgLayoutEngine {}.Layout(analysis, request);

    ASSERT_EQ(result.status, CfgLayoutStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.layout, nullptr);
    const auto routed = std::find_if(result.layout->edges.begin(), result.layout->edges.end(),
                                     [](const auto &edge) { return edge.kind == CfgEdgeKind::JUMP; });
    ASSERT_NE(routed, result.layout->edges.end());
    EXPECT_TRUE(routed->outerLane.has_value());
    ExpectRoutesAvoidNodeInteriors(*result.layout);
}

TEST(CfgLayoutEngineTest, ReroutesTerminalEdgeAroundPinnedObstacle)
{
    const auto analysis = Analyze({
        MakeInstruction(0U, ControlFlowKind::RETURN),
        MakeInstruction(1U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(analysis, nullptr);
    CfgLayoutRequest request;
    request.nodes.push_back({1U, 120.0, 60.0, CfgPoint {230.0, 200.0}});

    const auto result = CfgLayoutEngine {}.Layout(analysis, request);

    ASSERT_EQ(result.status, CfgLayoutStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.layout, nullptr);
    const auto routed = std::find_if(result.layout->edges.begin(), result.layout->edges.end(),
                                     [](const auto &edge) { return edge.edge == 0U; });
    ASSERT_NE(routed, result.layout->edges.end());
    ASSERT_GE(routed->points.size(), 2U);
    EXPECT_NE(routed->points.front().x, routed->points.back().x);
    ExpectRoutesAvoidNodeInteriors(*result.layout);
}

TEST(CfgLayoutEngineTest, RejectsPinnedRectangleOutsideCanvasLimitBeforeLayout)
{
    const auto analysis = Analyze({MakeInstruction(0U, ControlFlowKind::RETURN)});
    ASSERT_NE(analysis, nullptr);
    CfgLayoutLimits limits;
    CfgLayoutRequest request;
    request.nodes.push_back({0U, 420.0, 110.0, CfgPoint {limits.maxCanvasExtent - 100.0, 0.0}});

    const auto result = CfgLayoutEngine {}.Layout(analysis, request, limits);

    EXPECT_EQ(result.status, CfgLayoutStatus::INVALID_REQUEST);
    EXPECT_EQ(result.layout, nullptr);
}

TEST(CfgLayoutEngineTest, PlacesExceptionHandlersInTheRightSideColumn)
{
    auto disassembly = std::make_shared<FunctionDisassembly>();
    disassembly->codeSize = 5U;
    disassembly->instructions.push_back(MakeInstruction(0U, ControlFlowKind::NORMAL));
    auto throwing = MakeInstruction(1U, ControlFlowKind::NORMAL);
    throwing.canThrow = true;
    disassembly->instructions.push_back(std::move(throwing));
    disassembly->instructions.push_back(MakeInstruction(2U, ControlFlowKind::RETURN));
    disassembly->instructions.push_back(MakeInstruction(3U, ControlFlowKind::NORMAL));
    disassembly->instructions.push_back(MakeInstruction(4U, ControlFlowKind::RETURN));
    disassembly->tryRegions.emplace_back();
    disassembly->tryRegions.back().startPc = 0U;
    disassembly->tryRegions.back().endPc = 3U;
    disassembly->tryRegions.back().handlers.push_back({0U, 3U, 1U, 0U});
    const auto built = ControlFlowGraphBuilder {}.Build(disassembly);
    ASSERT_EQ(built.status, CfgStatus::SUCCESS) << built.diagnostic;
    const auto analyzed = CfgAnalyzer {}.Analyze(built.graph);
    ASSERT_EQ(analyzed.status, CfgAnalysisStatus::SUCCESS) << analyzed.diagnostic;

    const auto result = CfgLayoutEngine {}.Layout(analyzed.analysis);

    ASSERT_EQ(result.status, CfgLayoutStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.layout, nullptr);
    const auto *normal = FindNode(*result.layout, 0U);
    const auto *handler = FindNode(*result.layout, 1U);
    ASSERT_NE(normal, nullptr);
    ASSERT_NE(handler, nullptr);
    EXPECT_GT(handler->x, normal->x + normal->width);
}

TEST(CfgLayoutEngineTest, RejectsDuplicateOrNonFiniteNodeGeometry)
{
    const auto analysis = Analyze({MakeInstruction(0U, ControlFlowKind::RETURN)});
    ASSERT_NE(analysis, nullptr);
    CfgLayoutRequest request;
    request.nodes.push_back({0U, 400.0, 100.0, std::nullopt});
    request.nodes.push_back({0U, std::numeric_limits<double>::infinity(), 100.0, std::nullopt});

    const auto result = CfgLayoutEngine {}.Layout(analysis, request);

    EXPECT_EQ(result.status, CfgLayoutStatus::INVALID_REQUEST);
    EXPECT_EQ(result.layout, nullptr);
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
