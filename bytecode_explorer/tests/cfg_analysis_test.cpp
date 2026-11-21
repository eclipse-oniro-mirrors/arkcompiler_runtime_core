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

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

namespace panda::bytecode_explorer::test {
namespace {

InstructionInfo MakeInstruction(uint32_t pc, ControlFlowKind flowKind,
                                std::optional<uint32_t> jumpTarget = std::nullopt)
{
    InstructionInfo instruction;
    instruction.pc = pc;
    instruction.byteSize = 1U;
    instruction.mnemonic = "test";
    instruction.flowKind = flowKind;
    instruction.jumpTarget = jumpTarget;
    instruction.isTerminator = flowKind != ControlFlowKind::NORMAL;
    return instruction;
}

ControlFlowGraphPtr BuildGraph(std::initializer_list<InstructionInfo> instructions)
{
    auto disassembly = std::make_shared<FunctionDisassembly>();
    disassembly->codeSize = static_cast<uint32_t>(instructions.size());
    for (const auto &instruction : instructions) {
        disassembly->instructions.push_back(instruction);
    }
    const auto built = ControlFlowGraphBuilder {}.Build(disassembly);
    EXPECT_EQ(built.status, CfgStatus::SUCCESS) << built.diagnostic;
    return built.graph;
}

std::vector<BasicBlockId> CopyIds(const ModelVector<BasicBlockId> &ids)
{
    return {ids.begin(), ids.end()};
}

TEST(CfgAnalyzerTest, ComputesReachabilityAndImmediateDominatorsForDiamond)
{
    const auto graph = BuildGraph({
        MakeInstruction(0U, ControlFlowKind::CONDITIONAL_JUMP, 2U),
        MakeInstruction(1U, ControlFlowKind::UNCONDITIONAL_JUMP, 3U),
        MakeInstruction(2U, ControlFlowKind::UNCONDITIONAL_JUMP, 3U),
        MakeInstruction(3U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(graph, nullptr);

    const auto result = CfgAnalyzer {}.Analyze(graph);

    ASSERT_EQ(result.status, CfgAnalysisStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.analysis, nullptr);
    ASSERT_EQ(result.analysis->blockInfo.size(), 4U);
    EXPECT_TRUE(result.analysis->blockInfo[0].reachable);
    EXPECT_FALSE(result.analysis->blockInfo[0].immediateDominator.has_value());
    EXPECT_EQ(result.analysis->blockInfo[1].immediateDominator, 0U);
    EXPECT_EQ(result.analysis->blockInfo[2].immediateDominator, 0U);
    EXPECT_EQ(result.analysis->blockInfo[3].immediateDominator, 0U);
    EXPECT_EQ(result.analysis->blockInfo[3].dominatorDepth, 1U);
}

TEST(CfgAnalyzerTest, FindsNaturalLoopFromDominatorBackEdge)
{
    const auto graph = BuildGraph({
        MakeInstruction(0U, ControlFlowKind::NORMAL),
        MakeInstruction(1U, ControlFlowKind::CONDITIONAL_JUMP, 1U),
        MakeInstruction(2U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(graph, nullptr);

    const auto result = CfgAnalyzer {}.Analyze(graph);

    ASSERT_EQ(result.status, CfgAnalysisStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.analysis, nullptr);
    ASSERT_EQ(result.analysis->naturalLoops.size(), 1U);
    EXPECT_EQ(result.analysis->naturalLoops[0].header, 1U);
    EXPECT_EQ(CopyIds(result.analysis->naturalLoops[0].backEdgeSources), (std::vector<BasicBlockId> {1U}));
    EXPECT_EQ(CopyIds(result.analysis->naturalLoops[0].members), (std::vector<BasicBlockId> {1U}));
    EXPECT_TRUE(result.analysis->blockInfo[1].isLoopHeader);
    EXPECT_EQ(result.analysis->blockInfo[1].loopDepth, 1U);
    EXPECT_FALSE(result.analysis->blockInfo[1].isIrreducible);
}

TEST(CfgAnalyzerTest, MarksMultiEntryStronglyConnectedRegionIrreducible)
{
    const auto graph = BuildGraph({
        MakeInstruction(0U, ControlFlowKind::CONDITIONAL_JUMP, 2U),
        MakeInstruction(1U, ControlFlowKind::UNCONDITIONAL_JUMP, 3U),
        MakeInstruction(2U, ControlFlowKind::UNCONDITIONAL_JUMP, 3U),
        MakeInstruction(3U, ControlFlowKind::CONDITIONAL_JUMP, 1U),
        MakeInstruction(4U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(graph, nullptr);

    const auto result = CfgAnalyzer {}.Analyze(graph);

    ASSERT_EQ(result.status, CfgAnalysisStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.analysis, nullptr);
    const auto sccId = result.analysis->blockInfo[1].stronglyConnectedComponent;
    ASSERT_NE(sccId, INVALID_SCC_ID);
    EXPECT_EQ(result.analysis->blockInfo[3].stronglyConnectedComponent, sccId);
    const auto &component = result.analysis->stronglyConnectedComponents[sccId];
    EXPECT_TRUE(component.cyclic);
    EXPECT_TRUE(component.irreducible);
    EXPECT_EQ(CopyIds(component.members), (std::vector<BasicBlockId> {1U, 3U}));
    EXPECT_EQ(CopyIds(component.entryBlocks), (std::vector<BasicBlockId> {1U, 3U}));
    EXPECT_TRUE(result.analysis->blockInfo[1].isIrreducible);
    EXPECT_TRUE(result.analysis->blockInfo[3].isIrreducible);
    EXPECT_TRUE(result.analysis->naturalLoops.empty());
}

TEST(CfgAnalyzerTest, LeavesUnreachableBlocksOutsideReachableAnalyses)
{
    const auto graph = BuildGraph({
        MakeInstruction(0U, ControlFlowKind::RETURN),
        MakeInstruction(1U, ControlFlowKind::RETURN),
    });
    ASSERT_NE(graph, nullptr);

    const auto result = CfgAnalyzer {}.Analyze(graph);

    ASSERT_EQ(result.status, CfgAnalysisStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.analysis, nullptr);
    EXPECT_TRUE(result.analysis->blockInfo[0].reachable);
    EXPECT_FALSE(result.analysis->blockInfo[1].reachable);
    EXPECT_FALSE(result.analysis->blockInfo[1].immediateDominator.has_value());
    EXPECT_EQ(result.analysis->blockInfo[1].stronglyConnectedComponent, INVALID_SCC_ID);
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
