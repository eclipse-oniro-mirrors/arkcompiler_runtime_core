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

#include "core/cfg/cfg_builder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

namespace panda::bytecode_explorer::test {
namespace {

InstructionInfo MakeInstruction(uint32_t pc, uint32_t size, ControlFlowKind flowKind,
                                std::optional<uint32_t> jumpTarget = std::nullopt, bool canThrow = false)
{
    InstructionInfo instruction;
    instruction.pc = pc;
    instruction.byteSize = size;
    instruction.mnemonic = "test";
    instruction.flowKind = flowKind;
    instruction.jumpTarget = jumpTarget;
    instruction.canThrow = canThrow;
    instruction.isTerminator = flowKind != ControlFlowKind::NORMAL;
    return instruction;
}

std::shared_ptr<FunctionDisassembly> MakeBranchedDisassembly()
{
    auto disassembly = std::make_shared<FunctionDisassembly>();
    disassembly->codeSize = 7U;
    disassembly->instructions.push_back(MakeInstruction(0U, 2U, ControlFlowKind::CONDITIONAL_JUMP, 4U));
    disassembly->instructions.push_back(MakeInstruction(2U, 2U, ControlFlowKind::UNCONDITIONAL_JUMP, 4U));
    disassembly->instructions.push_back(MakeInstruction(4U, 1U, ControlFlowKind::RETURN));
    disassembly->instructions.push_back(MakeInstruction(5U, 2U, ControlFlowKind::THROW, std::nullopt, true));
    return disassembly;
}

const ControlFlowEdge *FindEdge(const ControlFlowGraph &graph, BasicBlockId source, CfgEdgeKind kind)
{
    const auto found =
        std::find_if(graph.edges.begin(), graph.edges.end(), [source, kind](const ControlFlowEdge &edge) {
            return edge.source == source && edge.kind == kind;
        });
    return found == graph.edges.end() ? nullptr : &*found;
}

TEST(ControlFlowGraphBuilderTest, SplitsLeadersAndBuildsTypedNormalEdges)
{
    const auto result = ControlFlowGraphBuilder {}.Build(MakeBranchedDisassembly());

    ASSERT_EQ(result.status, CfgStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.graph, nullptr);
    ASSERT_EQ(result.graph->blocks.size(), 4U);
    EXPECT_EQ(result.graph->entryBlock, 0U);
    EXPECT_EQ(result.graph->blocks[0].startPc, 0U);
    EXPECT_EQ(result.graph->blocks[0].endPc, 2U);
    EXPECT_EQ(result.graph->blocks[1].startPc, 2U);
    EXPECT_EQ(result.graph->blocks[2].startPc, 4U);
    EXPECT_EQ(result.graph->blocks[3].startPc, 5U);

    ASSERT_EQ(result.graph->edges.size(), 5U);
    const auto *trueEdge = FindEdge(*result.graph, 0U, CfgEdgeKind::TRUE_BRANCH);
    const auto *falseEdge = FindEdge(*result.graph, 0U, CfgEdgeKind::FALSE_BRANCH);
    const auto *jumpEdge = FindEdge(*result.graph, 1U, CfgEdgeKind::JUMP);
    const auto *returnEdge = FindEdge(*result.graph, 2U, CfgEdgeKind::RETURN);
    const auto *throwEdge = FindEdge(*result.graph, 3U, CfgEdgeKind::THROW);
    ASSERT_NE(trueEdge, nullptr);
    ASSERT_NE(falseEdge, nullptr);
    ASSERT_NE(jumpEdge, nullptr);
    ASSERT_NE(returnEdge, nullptr);
    ASSERT_NE(throwEdge, nullptr);
    EXPECT_EQ(trueEdge->target, 2U);
    EXPECT_EQ(falseEdge->target, 1U);
    EXPECT_EQ(jumpEdge->target, 2U);
    EXPECT_FALSE(returnEdge->target.has_value());
    EXPECT_FALSE(throwEdge->target.has_value());
}

TEST(ControlFlowGraphBuilderTest, BuildsExceptionEdgesOnlyFromProtectedThrowingBlocks)
{
    auto disassembly = std::make_shared<FunctionDisassembly>();
    disassembly->codeSize = 5U;
    disassembly->instructions.push_back(MakeInstruction(0U, 1U, ControlFlowKind::NORMAL));
    disassembly->instructions.push_back(MakeInstruction(1U, 1U, ControlFlowKind::NORMAL, std::nullopt, true));
    disassembly->instructions.push_back(MakeInstruction(2U, 1U, ControlFlowKind::RETURN));
    disassembly->instructions.push_back(MakeInstruction(3U, 1U, ControlFlowKind::NORMAL));
    disassembly->instructions.push_back(MakeInstruction(4U, 1U, ControlFlowKind::RETURN));
    disassembly->tryRegions.emplace_back();
    auto &tryRegion = disassembly->tryRegions.back();
    tryRegion.startPc = 0U;
    tryRegion.endPc = 3U;
    tryRegion.handlers.push_back({17U, 3U, 1U, 0U});

    const auto result = ControlFlowGraphBuilder {}.Build(disassembly);

    ASSERT_EQ(result.status, CfgStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.graph, nullptr);
    ASSERT_EQ(result.graph->blocks.size(), 3U);
    EXPECT_FALSE(result.graph->blocks[0].isHandler);
    EXPECT_TRUE(result.graph->blocks[0].hasThrowingInstruction);
    EXPECT_TRUE(result.graph->blocks[1].isHandler);
    EXPECT_EQ(result.graph->blocks[1].endPc, 4U);
    EXPECT_FALSE(result.graph->blocks[2].isHandler);
    const auto *exceptionEdge = FindEdge(*result.graph, 0U, CfgEdgeKind::EXCEPTION);
    ASSERT_NE(exceptionEdge, nullptr);
    EXPECT_EQ(exceptionEdge->target, 1U);
    EXPECT_EQ(exceptionEdge->tryRegionIndex, 0U);
    EXPECT_EQ(exceptionEdge->handlerIndex, 0U);
    EXPECT_EQ(exceptionEdge->catchTypeIndex, 17U);
}

TEST(ControlFlowGraphBuilderTest, RejectsDisassemblyWithInstructionGap)
{
    auto disassembly = std::make_shared<FunctionDisassembly>();
    disassembly->codeSize = 3U;
    disassembly->instructions.push_back(MakeInstruction(0U, 1U, ControlFlowKind::NORMAL));
    disassembly->instructions.push_back(MakeInstruction(2U, 1U, ControlFlowKind::RETURN));

    const auto result = ControlFlowGraphBuilder {}.Build(disassembly);

    EXPECT_EQ(result.status, CfgStatus::INVALID_DISASSEMBLY);
    EXPECT_EQ(result.graph, nullptr);
    EXPECT_NE(result.diagnostic.find("contiguous"), std::string::npos);
}

TEST(ControlFlowGraphBuilderTest, EnforcesConfiguredBlockLimit)
{
    CfgLimits limits;
    limits.maxBlockCount = 2U;

    const auto result = ControlFlowGraphBuilder {}.Build(MakeBranchedDisassembly(), limits);

    EXPECT_EQ(result.status, CfgStatus::RESOURCE_LIMIT_EXCEEDED);
    EXPECT_EQ(result.graph, nullptr);
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
