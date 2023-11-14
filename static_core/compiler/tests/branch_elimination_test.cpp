/**
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#include "unit_test.h"
#include "optimizer/optimizations/branch_elimination.h"
#include "optimizer/optimizations/cleanup.h"

namespace panda::compiler {
enum class RemainedSuccessor { TRUE_SUCCESSOR, FALSE_SUCCESSOR, BOTH };
enum class DominantCondResult { FALSE, TRUE };
enum class SwapInputs { FALSE, TRUE };
enum class SwapCC { FALSE, TRUE };

// NOLINTBEGIN(readability-magic-numbers)
class BranchEliminationTest : public GraphTest {
public:
    template <uint64_t CONST_CONDITION_BLOCK_ID, uint64_t CONST_VALUE, SwapCC SWAP_CC = SwapCC::FALSE>
    void BuildTestGraph(Graph *graph);

    template <uint64_t CONST_CONDITION_BLOCK_ID, uint64_t CONST_VALUE>
    void BuildTestGraph2(Graph *graph);

    template <DominantCondResult DOM_RESULT, RemainedSuccessor ELIM_SUCC, SwapInputs SWAP_INPUTS = SwapInputs::FALSE>
    void BuildGraphAndCheckElimination(ConditionCode dominant_code, ConditionCode code);

    template <DominantCondResult DOM_RESULT, SwapInputs SWAP_INPUTS>
    void BuildContitionsCheckGraph(Graph *graph, ConditionCode dominant_code, ConditionCode code);
    template <DominantCondResult DOM_RESULT, SwapInputs SWAP_INPUTS>
    void BuildContitionsCheckGraphElimTrueSucc(Graph *graph, ConditionCode dominant_code, ConditionCode code);
    template <DominantCondResult DOM_RESULT, SwapInputs SWAP_INPUTS>
    void BuildContitionsCheckGraphElimFalseSucc(Graph *graph, ConditionCode dominant_code, ConditionCode code);

protected:
    void InitBlockToBeDisconnected(std::vector<BasicBlock *> &&blocks)
    {
        disconnected_blocks_ = std::move(blocks);
        removed_instructions_.clear();
        for (const auto &block : disconnected_blocks_) {
            for (auto inst : block->AllInsts()) {
                removed_instructions_.push_back(inst);
            }
        }
    }

    void CheckBlocksDisconnected()
    {
        for (const auto &block : disconnected_blocks_) {
            EXPECT_TRUE(block->GetGraph() == nullptr);
            EXPECT_TRUE(block->IsEmpty());
            EXPECT_EQ(block->GetSuccsBlocks().size(), 0U);
            EXPECT_EQ(block->GetPredsBlocks().size(), 0U);
            for (auto inst : block->AllInsts()) {
                EXPECT_TRUE(inst->GetBasicBlock() == nullptr);
                EXPECT_TRUE(inst->GetUsers().Empty());
                for (auto input : inst->GetInputs()) {
                    EXPECT_TRUE(input.GetInst() == nullptr);
                }
            }
        }
    }

private:
    std::vector<BasicBlock *> disconnected_blocks_;
    std::vector<Inst *> removed_instructions_;
};

/*
 *             [0]
 *              |
 *        /----[2]----\
 *        |           |
 *        v           v
 *       [3]    /----[4]----\
 *        |     |           |
 *        |    [5]         [6]
 *        |     |           |
 *        v     v           |
 *      [exit]<-------------/
 */
template <uint64_t CONST_CONDITION_BLOCK_ID, uint64_t CONST_VALUE, SwapCC SWAP_CC>
void BranchEliminationTest::BuildTestGraph(Graph *graph)
{
    GRAPH(graph)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();
        CONSTANT(3U, CONST_VALUE);

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(19U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(19U);
        }
        BASIC_BLOCK(3U, 7U)
        {
            INST(5U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(6U, Opcode::Add).u64().Inputs(5U, 2U);
        }
        BASIC_BLOCK(4U, 5U, 6U)
        {
            INST(9U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 2U);
            INST(10U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(9U);
        }
        BASIC_BLOCK(5U, 7U)
        {
            INST(11U, Opcode::Sub).u64().Inputs(0U, 1U);
            INST(12U, Opcode::Sub).u64().Inputs(11U, 2U);
        }
        BASIC_BLOCK(6U, 7U)
        {
            INST(14U, Opcode::Mul).u64().Inputs(0U, 1U);
            INST(15U, Opcode::Mul).u64().Inputs(14U, 2U);
        }
        BASIC_BLOCK(7U, -1L)
        {
            INST(17U, Opcode::Phi).u64().Inputs(6U, 12U, 15U);
            INST(18U, Opcode::Return).u64().Inputs(17U);
        }
    }

    BB(CONST_CONDITION_BLOCK_ID).SetTry(true);
    auto inst_if = BB(CONST_CONDITION_BLOCK_ID).GetLastInst();
    ASSERT_TRUE(inst_if->GetOpcode() == Opcode::IfImm);
    inst_if->SetInput(0U, &INS(3U));

    if constexpr (SWAP_CC == SwapCC::TRUE) {
        INS(4U).CastToIfImm()->SetCc(CC_EQ);
        INS(10U).CastToIfImm()->SetCc(CC_EQ);
    }
}

/*
 *              [0]
 *               |
 *        /-----[2]-----\
 *        |             |
 *        |             v
 *        |       /----[4]----\
 *        |       |           |
 *       [3]---->[5]         [6]
 *        |       |           |
 *        v       v           |
 *      [exit]<---------------/
 */
template <uint64_t CONST_CONDITION_BLOCK_ID, uint64_t CONST_VALUE>
void BranchEliminationTest::BuildTestGraph2(Graph *graph)
{
    GRAPH(graph)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();
        CONSTANT(3U, CONST_VALUE);

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(19U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(19U);
        }
        BASIC_BLOCK(3U, 7U, 5U)
        {
            INST(5U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(6U, Opcode::Add).u64().Inputs(5U, 2U);
            INST(20U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 2U);
            INST(21U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(20U);
        }
        BASIC_BLOCK(4U, 5U, 6U)
        {
            INST(9U, Opcode::Compare).b().CC(CC_EQ).Inputs(1U, 2U);
            INST(10U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(9U);
        }
        BASIC_BLOCK(5U, 7U)
        {
            INST(11U, Opcode::Phi).u64().Inputs(0U, 1U);
            INST(12U, Opcode::Sub).u64().Inputs(11U, 2U);
        }
        BASIC_BLOCK(6U, 7U)
        {
            INST(14U, Opcode::Mul).u64().Inputs(0U, 1U);
            INST(15U, Opcode::Mul).u64().Inputs(14U, 2U);
        }
        BASIC_BLOCK(7U, -1L)
        {
            INST(17U, Opcode::Phi).u64().Inputs(6U, 12U, 15U);
            INST(18U, Opcode::Return).u64().Inputs(17U);
        }
    }
    auto inst_if = BB(CONST_CONDITION_BLOCK_ID).GetLastInst();
    ASSERT_TRUE(inst_if->GetOpcode() == Opcode::IfImm);
    inst_if->SetInput(0U, &INS(3U));
}

/*
 *             [0]
 *              |
 *        /----[2]----\
 *        |           |
 *        v           v
 *       [3]    /----[4]----\
 *        |     |           |
 *        |    [5]         [6]
 *        |     |           |
 *        v     v           |
 *      [exit]<-------------/
 *
 *  Disconnect branch: [4, 5, 6]
 */
TEST_F(BranchEliminationTest, DisconnectFalseBranch)
{
    static constexpr uint64_t CONST_CONDITION_BLOCK_ID = 2;
    static constexpr uint64_t CONSTANT_VALUE = 1;
    BuildTestGraph<CONST_CONDITION_BLOCK_ID, CONSTANT_VALUE>(GetGraph());
    InitBlockToBeDisconnected({&BB(4U), &BB(5U), &BB(6U)});

    GetGraph()->RunPass<BranchElimination>();
    CheckBlocksDisconnected();
    EXPECT_EQ(BB(2U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(2U).GetSuccessor(0U), &BB(3U));
    EXPECT_FALSE(INS(17U).HasUsers());
    EXPECT_EQ(INS(18U).GetInput(0U).GetInst(), &INS(6U));

    auto graph = CreateEmptyGraph();
    BuildTestGraph<CONST_CONDITION_BLOCK_ID, CONSTANT_VALUE, SwapCC::TRUE>(graph);
    InitBlockToBeDisconnected({&BB(3U)});
    graph->RunPass<BranchElimination>();
    CheckBlocksDisconnected();
    EXPECT_EQ(BB(2U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(2U).GetSuccessor(0U), &BB(4U));

    auto phi = &INS(17U);
    EXPECT_TRUE(phi->HasUsers());
    EXPECT_EQ(phi->GetInputsCount(), 2U);
    EXPECT_EQ(INS(12U).GetUsers().Front().GetInst(), phi);
    EXPECT_EQ(INS(15U).GetUsers().Front().GetInst(), phi);
}

/*
 *             [0]
 *              |
 *        /----[2]----\
 *        |           |
 *        v           v
 *       [3]    /----[4]----\
 *        |     |           |
 *        |    [5]         [6]
 *        |     |           |
 *        v     v           |
 *      [exit]<-------------/
 *
 *  Disconnect branch: [3]
 */
TEST_F(BranchEliminationTest, DisconnectTrueBranch)
{
    static constexpr uint64_t CONST_CONDITION_BLOCK_ID = 2;
    static constexpr uint64_t CONSTANT_VALUE = 0;
    BuildTestGraph<CONST_CONDITION_BLOCK_ID, CONSTANT_VALUE>(GetGraph());
    InitBlockToBeDisconnected({&BB(3U)});

    GetGraph()->RunPass<BranchElimination>();
    CheckBlocksDisconnected();
    EXPECT_EQ(BB(2U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(2U).GetSuccessor(0U), &BB(4U));

    auto phi = &INS(17U);
    EXPECT_TRUE(phi->HasUsers());
    EXPECT_EQ(phi->GetInputsCount(), 2U);
    EXPECT_EQ(INS(12U).GetUsers().Front().GetInst(), phi);
    EXPECT_EQ(INS(15U).GetUsers().Front().GetInst(), phi);

    auto graph = CreateEmptyGraph();
    BuildTestGraph<CONST_CONDITION_BLOCK_ID, CONSTANT_VALUE, SwapCC::TRUE>(graph);
    InitBlockToBeDisconnected({&BB(4U), &BB(5U), &BB(6U)});

    graph->RunPass<BranchElimination>();
    CheckBlocksDisconnected();
    EXPECT_EQ(BB(2U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(2U).GetSuccessor(0U), &BB(3U));
    EXPECT_FALSE(INS(17U).HasUsers());
    EXPECT_EQ(INS(18U).GetInput(0U).GetInst(), &INS(6U));
}

/*
 *             [0]
 *              |
 *        /----[2]----\
 *        |           |
 *        v           v
 *       [3]    /----[4]----\
 *        |     |           |
 *        |    [5]         [6]
 *        |     |           |
 *        v     v           |
 *      [exit]<-------------/
 *
 *  Disconnect branch: [6]
 */
TEST_F(BranchEliminationTest, DisconnectInnerFalseBranch)
{
    static constexpr uint64_t CONST_CONDITION_BLOCK_ID = 4;
    static constexpr uint64_t CONSTANT_VALUE = 1;
    BuildTestGraph<CONST_CONDITION_BLOCK_ID, CONSTANT_VALUE>(GetGraph());
    InitBlockToBeDisconnected({&BB(6U)});

    GetGraph()->RunPass<BranchElimination>();
    CheckBlocksDisconnected();
    EXPECT_EQ(BB(4U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(4U).GetSuccessor(0U), &BB(5U));

    auto phi = &INS(17U);
    EXPECT_TRUE(phi->HasUsers());
    EXPECT_EQ(phi->GetInputsCount(), 2U);
    EXPECT_EQ(INS(6U).GetUsers().Front().GetInst(), phi);
    EXPECT_EQ(INS(12U).GetUsers().Front().GetInst(), phi);
}

/*
 *             [0]
 *              |
 *        /----[2]----\
 *        |           |
 *        v           v
 *       [3]    /----[4]----\
 *        |     |           |
 *        |    [5]         [6]
 *        |     |           |
 *        v     v           |
 *      [exit]<-------------/
 *
 *  Disconnect branch: [5]
 */
TEST_F(BranchEliminationTest, DisconnectInnerTrueBranch)
{
    static constexpr uint64_t CONST_CONDITION_BLOCK_ID = 4;
    static constexpr uint64_t CONSTANT_VALUE = 0;
    BuildTestGraph<CONST_CONDITION_BLOCK_ID, CONSTANT_VALUE>(GetGraph());
    InitBlockToBeDisconnected({&BB(5U)});

    GetGraph()->RunPass<BranchElimination>();
    CheckBlocksDisconnected();
    EXPECT_EQ(BB(4U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(4U).GetSuccessor(0U), &BB(6U));

    auto phi = &INS(17U);
    EXPECT_TRUE(phi->HasUsers());
    EXPECT_EQ(phi->GetInputsCount(), 2U);
    EXPECT_EQ(INS(6U).GetUsers().Front().GetInst(), phi);
    EXPECT_EQ(INS(15U).GetUsers().Front().GetInst(), phi);
}

/*
 *              [0]
 *               |
 *        /-----[2]-----\
 *        |             |
 *        |             v
 *        |       /----[4]----\
 *        |       |           |
 *       [3]---->[5]         [6]
 *        |       |           |
 *        v       v           |
 *      [exit]<---------------/
 *
 *  Disconnect branch: [4, 6]
 */
TEST_F(BranchEliminationTest, RemoveBranchPart)
{
    static constexpr uint64_t CONST_CONDITION_BLOCK_ID = 2;
    static constexpr uint64_t CONSTANT_VALUE = 1;
    BuildTestGraph2<CONST_CONDITION_BLOCK_ID, CONSTANT_VALUE>(GetGraph());
    InitBlockToBeDisconnected({&BB(4U), &BB(6U)});

    GetGraph()->RunPass<BranchElimination>();
    CheckBlocksDisconnected();
    EXPECT_EQ(BB(2U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(2U).GetSuccessor(0U), &BB(3U));
    EXPECT_EQ(BB(5U).GetPredsBlocks().size(), 1U);
    EXPECT_EQ(BB(5U).GetPredBlockByIndex(0U), &BB(3U));

    auto phi = &INS(17U);
    EXPECT_TRUE(phi->HasUsers());
    EXPECT_EQ(phi->GetInputsCount(), 2U);
    EXPECT_EQ(INS(6U).GetUsers().Front().GetInst(), phi);
    EXPECT_EQ(INS(12U).GetUsers().Front().GetInst(), phi);
}

/*
 *              [0]
 *               |
 *        /-----[2]-----\
 *        |             |
 *        |             v
 *        |       /----[4]----\
 *        |       |           |
 *       [3]---->[5]         [6]
 *        |       |           |
 *        v       v           |
 *      [exit]<---------------/
 *
 *  Remove edge between [4] and [5]
 */
TEST_F(BranchEliminationTest, RemoveEdge)
{
    static constexpr uint64_t CONST_CONDITION_BLOCK_ID = 4;
    static constexpr uint64_t CONSTANT_VALUE = 0;
    BuildTestGraph2<CONST_CONDITION_BLOCK_ID, CONSTANT_VALUE>(GetGraph());

    GetGraph()->RunPass<BranchElimination>();

    EXPECT_EQ(BB(4U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(4U).GetSuccessor(0U), &BB(6U));
    EXPECT_EQ(BB(5U).GetPredsBlocks().size(), 1U);
    EXPECT_EQ(BB(5U).GetPredBlockByIndex(0U), &BB(3U));

    auto phi = &INS(17U);
    EXPECT_TRUE(phi->HasUsers());
    EXPECT_EQ(phi->GetInputsCount(), 3U);
    EXPECT_EQ(INS(6U).GetUsers().Front().GetInst(), phi);
    EXPECT_EQ(INS(12U).GetUsers().Front().GetInst(), phi);
    EXPECT_EQ(INS(15U).GetUsers().Front().GetInst(), phi);
}

/*
 *              [0]
 *               |
 *        /-----[2]-----\
 *        |             |
 *        |             v
 *        |       /----[4]----\
 *        |       |           |
 *       [3]---->[5]         [6]
 *        |       |           |
 *        v       v           |
 *      [exit]<---------------/
 *
 *  Remove branches [3-5] and [4-5]
 *
 *              [0]
 *               |
 *              [2]
 *               |
 *              [4]
 *               |
 *              [6]
 *               |
 *             [exit]
 */
TEST_F(BranchEliminationTest, RemoveEdgeAndWholeBlock)
{
    static constexpr uint64_t CONST_CONDITION_BLOCK_ID = 2;
    static constexpr uint64_t CONSTANT_VALUE = 0;
    BuildTestGraph2<CONST_CONDITION_BLOCK_ID, CONSTANT_VALUE>(GetGraph());
    BB(4U).GetLastInst()->SetInput(0U, &INS(3U));
    InitBlockToBeDisconnected({&BB(3U), &BB(5U)});

    GetGraph()->RunPass<BranchElimination>();
    CheckBlocksDisconnected();

    EXPECT_EQ(BB(0U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(2U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(4U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(6U).GetSuccsBlocks().size(), 1U);

    auto phi = &INS(17U);
    EXPECT_FALSE(phi->HasUsers());
}

/*
 *                  [0]
 *                   |
 *            /-----[2]-----\
 *            |             |
 *            v             v
 *      /----[3]----\  /----[4]----\
 *      |           |  |           |
 *     [5]         [6][7]         [8]
 *      |           |  |           |
 *      |           v  v           |
 *      |           [9]            |
 *      |            v             |
 *      \--------->[exit]<---------/
 *
 * Remove branches [3-6], [4-7] and as a result remove block [9]
 */
TEST_F(BranchEliminationTest, DisconnectPredecessors)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).b();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();
        CONSTANT(3U, 0U);
        CONSTANT(4U, 1U);

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(5U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(0U);
        }
        BASIC_BLOCK(3U, 5U, 6U)
        {
            INST(6U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(4U);
        }
        BASIC_BLOCK(4U, 7U, 8U)
        {
            INST(7U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(3U);
        }
        BASIC_BLOCK(6U, 9U) {}
        BASIC_BLOCK(7U, 9U) {}
        BASIC_BLOCK(5U, 10U)
        {
            INST(10U, Opcode::Add).u64().Inputs(1U, 2U);
        }
        BASIC_BLOCK(8U, 10U)
        {
            INST(12U, Opcode::Sub).u64().Inputs(1U, 2U);
        }
        BASIC_BLOCK(9U, 10U)
        {
            INST(14U, Opcode::Mul).u64().Inputs(1U, 2U);
        }
        BASIC_BLOCK(10U, -1L)
        {
            INST(16U, Opcode::Phi).u64().Inputs(10U, 12U, 14U);
            INST(17U, Opcode::Return).u64().Inputs(16U);
        }
    }

    InitBlockToBeDisconnected({&BB(6U), &BB(7U), &BB(9U)});

    GetGraph()->RunPass<BranchElimination>();
    CheckBlocksDisconnected();

    EXPECT_EQ(BB(3U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(3U).GetSuccessor(0U), &BB(5U));
    EXPECT_EQ(BB(4U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(4U).GetSuccessor(0U), &BB(8U));
    EXPECT_EQ(BB(10U).GetPredsBlocks().size(), 2U);

    auto phi = &INS(16U);
    EXPECT_EQ(phi->GetInputsCount(), 2U);
    EXPECT_EQ(INS(10U).GetUsers().Front().GetInst(), phi);
    EXPECT_EQ(INS(12U).GetUsers().Front().GetInst(), phi);
}

/*
 *           [0]
 *            |
 *            v
 *      /--->[2]----\
 *      |     |     |
 *      |     v     v
 *      \----[3]  [exit]
 *
 * Remove [3]
 *
 * [0] -> [2] -> [exit]
 *
 */
TEST_F(BranchEliminationTest, RemoveLoopBackEdge)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).b();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();
        CONSTANT(3U, 0U);

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(5U, Opcode::Phi).u64().Inputs(1U, 9U);
            INST(6U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(3U);
        }
        BASIC_BLOCK(3U, 2U)
        {
            INST(8U, Opcode::Add).u64().Inputs(5U, 2U);
            INST(9U, Opcode::Add).u64().Inputs(8U, 2U);
        }
        BASIC_BLOCK(4U, -1L)
        {
            INST(11U, Opcode::Return).u64().Inputs(5U);
        }
    }

    InitBlockToBeDisconnected({&BB(3U)});

    GetGraph()->RunPass<BranchElimination>();
    CheckBlocksDisconnected();
    EXPECT_EQ(BB(0U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(2U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(INS(11U).GetInput(0U).GetInst(), &INS(1U));
}

/*
 *           [0]
 *            |
 *            v
 *      /--->[2]
 *      |     | \
 *      |     |  \
 *      \----/    \
 *                 |
 *                 v
 *               [exit]
 *
 * Remove loop edge [2->2]
 *
 * [0] -> [2] -> [exit]
 */
TEST_F(BranchEliminationTest, RemoveOneBlockLoopExit)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).b();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();
        CONSTANT(3U, 0U);

        BASIC_BLOCK(2U, 2U, 4U)
        {
            INST(5U, Opcode::Phi).u64().Inputs(1U, 9U);
            INST(8U, Opcode::Add).u64().Inputs(5U, 2U);
            INST(9U, Opcode::Add).u64().Inputs(8U, 2U);
            INST(6U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(3U);
        }
        BASIC_BLOCK(4U, -1L)
        {
            INST(11U, Opcode::Return).u64().Inputs(9U);
        }
    }

    GetGraph()->RunPass<BranchElimination>();

    EXPECT_EQ(BB(0U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(2U).GetSuccsBlocks().size(), 1U);

    EXPECT_FALSE(INS(5U).HasUsers());
    EXPECT_TRUE(INS(8U).HasUsers());
    EXPECT_TRUE(INS(9U).HasUsers());
    EXPECT_EQ(INS(8U).GetInput(0U).GetInst(), &INS(1U));
}

/*
 *           [0]
 *            |
 *            v
 *      /--->[2]
 *      |     |
 *      |     v
 *      \----[3]----\
 *                  |
 *                  v
 *                [exit]
 *
 * Remove edge [3-2]
 *
 * [0] -> [2] -> [3] -> [exit]
 */
TEST_F(BranchEliminationTest, RemoveLoopExit)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).b();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();
        CONSTANT(3U, 0U);

        BASIC_BLOCK(2U, 5U, 6U)
        {
            INST(5U, Opcode::Phi).u64().Inputs(1U, 9U);
            INST(20U, Opcode::SaveState).NoVregs();
            INST(7U, Opcode::CallStatic).v0id().InputsAutoType(20U);
            INST(4U, Opcode::If).SrcType(DataType::Type::UINT64).CC(CC_NE).Inputs(1U, 2U);
        }
        BASIC_BLOCK(5U, 3U)
        {
            INST(21U, Opcode::SaveState).NoVregs();
            INST(10U, Opcode::CallStatic).v0id().InputsAutoType(21U);
        }
        BASIC_BLOCK(6U, 3U)
        {
            INST(22U, Opcode::SaveState).NoVregs();
            INST(11U, Opcode::CallStatic).v0id().InputsAutoType(22U);
        }
        BASIC_BLOCK(3U, 2U, 4U)
        {
            INST(8U, Opcode::Add).u64().Inputs(5U, 2U);
            INST(9U, Opcode::Add).u64().Inputs(8U, 2U);
            INST(6U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(3U);
        }
        BASIC_BLOCK(4U, -1L)
        {
            INST(12U, Opcode::Return).u64().Inputs(9U);
        }
    }

    GetGraph()->RunPass<BranchElimination>();

    EXPECT_EQ(BB(0U).GetSuccsBlocks().size(), 1U);
    EXPECT_EQ(BB(2U).GetSuccsBlocks().size(), 2U);
    EXPECT_EQ(BB(3U).GetSuccsBlocks().size(), 1U);

    EXPECT_FALSE(INS(5U).HasUsers());
    EXPECT_TRUE(INS(8U).HasUsers());
    EXPECT_TRUE(INS(9U).HasUsers());
    EXPECT_EQ(INS(8U).GetInput(0U).GetInst(), &INS(1U));
}

/*
 *              [0]
 *            T  |  F
 *        /-----[2]-----\
 *        |             |
 *        v             v
 *       [3]<----------[4]<----\
 *        |             |      |
 *        v             v      |
 *      [exit]         [5]-----/
 *
 * Transform to [0]-->[2]-->[exit]
 */
TEST_F(BranchEliminationTest, RemoveEdgeToLoop)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).b();
        CONSTANT(3U, 1U);

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(3U);
        }
        BASIC_BLOCK(3U, -1L)
        {
            INST(5U, Opcode::Phi).u64().Inputs(0U, 7U);
            INST(6U, Opcode::Return).u64().Inputs(5U);
        }
        BASIC_BLOCK(4U, 5U, 3U)
        {
            INST(7U, Opcode::Phi).u64().Inputs({{2U, 0U}, {5U, 9U}});
            INST(8U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(2U);
        }
        BASIC_BLOCK(5U, 4U)
        {
            INST(9U, Opcode::Add).u64().Inputs(7U, 0U);
        }
    }
    /* Pretend loop unrolling is done to allow branch elimination to mutilate
     * loop preheader, it has to be removed once loop unroller starts dealing
     * with loops with the empty preheaders */
    GetGraph()->SetUnrollComplete();

    GetGraph()->RunPass<BranchElimination>();
    GetGraph()->RunPass<Cleanup>();

    auto expected_graph = CreateEmptyGraph();
    GRAPH(expected_graph)
    {
        PARAMETER(0U, 0U).u64();
        BASIC_BLOCK(2U, -1L)
        {
            INST(1U, Opcode::Return).u64().Inputs(0U);
        }
    }

    ASSERT_TRUE(GraphComparator().Compare(GetGraph(), expected_graph));
}

/*
 *             [0]
 *           T  |  F
 *        /----[2]----\
 *        |           |
 *        v        T  v  F
 *       [3]    /----[4]----\
 *        |     |           |
 *        |    [5]         [6]
 *        |     |           |
 *        v     v           |
 *      [exit]<-------------/
 */
template <DominantCondResult DOM_RESULT, SwapInputs SWAP_INPUTS>
void BranchEliminationTest::BuildContitionsCheckGraph(Graph *graph, ConditionCode dominant_code, ConditionCode code)
{
    GRAPH(graph)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(19U, Opcode::Compare).b().CC(dominant_code).Inputs(0U, 1U);
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(19U);
        }
        BASIC_BLOCK(3U, 7U)
        {
            INST(5U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(6U, Opcode::Add).u64().Inputs(5U, 2U);
        }
        BASIC_BLOCK(4U, 5U, 6U)
        {
            INST(9U, Opcode::Compare).b().CC(code).Inputs(0U, 1U);
            INST(10U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(9U);
        }
        BASIC_BLOCK(5U, 7U)
        {
            INST(11U, Opcode::Sub).u64().Inputs(0U, 1U);
            INST(12U, Opcode::Sub).u64().Inputs(11U, 2U);
        }
        BASIC_BLOCK(6U, 7U)
        {
            INST(14U, Opcode::Mul).u64().Inputs(0U, 1U);
            INST(15U, Opcode::Mul).u64().Inputs(14U, 2U);
        }
        BASIC_BLOCK(7U, -1L)
        {
            INST(17U, Opcode::Phi).u64().Inputs(6U, 12U, 15U);
            INST(18U, Opcode::Return).u64().Inputs(17U);
        }
    }
    if constexpr (DOM_RESULT == DominantCondResult::TRUE) {
        BB(2U).SwapTrueFalseSuccessors();
    }
    if constexpr (SWAP_INPUTS == SwapInputs::TRUE) {
        INS(19U).SwapInputs();
    }
}

/*
 *             [0]
 *              |
 *        /----[2]----\
 *        |           |
 *        v           v
 *       [3]         [4]
 *        |           |
 *        |          [5]
 *        |           |
 *        v           |
 *      [exit]<-------/
 */
template <DominantCondResult DOM_RESULT, SwapInputs SWAP_INPUTS>
void BranchEliminationTest::BuildContitionsCheckGraphElimFalseSucc(Graph *graph, ConditionCode dominant_code,
                                                                   ConditionCode code)
{
    GRAPH(graph)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(19U, Opcode::Compare).b().CC(dominant_code).Inputs(0U, 1U);
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(19U);
        }
        BASIC_BLOCK(3U, 7U)
        {
            INST(5U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(6U, Opcode::Add).u64().Inputs(5U, 2U);
        }
        BASIC_BLOCK(4U, 5U)
        {
            INST(9U, Opcode::Compare).b().CC(code).Inputs(0U, 1U);
        }
        BASIC_BLOCK(5U, 7U)
        {
            INST(11U, Opcode::Sub).u64().Inputs(0U, 1U);
            INST(12U, Opcode::Sub).u64().Inputs(11U, 2U);
        }
        BASIC_BLOCK(7U, -1L)
        {
            INST(17U, Opcode::Phi).u64().Inputs(6U, 12U);
            INST(18U, Opcode::Return).u64().Inputs(17U);
        }
    }
    if constexpr (DOM_RESULT == DominantCondResult::TRUE) {
        BB(2U).SwapTrueFalseSuccessors();
    }
    if constexpr (SWAP_INPUTS == SwapInputs::TRUE) {
        INS(19U).SwapInputs();
    }
}

/*
 *             [0]
 *              |
 *        /----[2]----\
 *        |           |
 *        v           v
 *       [3]         [4]----\
 *        |                 |
 *        |                [6]
 *        |                 |
 *        v                 |
 *      [exit]<-------------/
 */
template <DominantCondResult DOM_RESULT, SwapInputs SWAP_INPUTS>
void BranchEliminationTest::BuildContitionsCheckGraphElimTrueSucc(Graph *graph, ConditionCode dominant_code,
                                                                  ConditionCode code)
{
    GRAPH(graph)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(19U, Opcode::Compare).b().CC(dominant_code).Inputs(0U, 1U);
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(19U);
        }
        BASIC_BLOCK(3U, 7U)
        {
            INST(5U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(6U, Opcode::Add).u64().Inputs(5U, 2U);
        }
        BASIC_BLOCK(4U, 6U)
        {
            INST(9U, Opcode::Compare).b().CC(code).Inputs(0U, 1U);
        }
        BASIC_BLOCK(6U, 7U)
        {
            INST(14U, Opcode::Mul).u64().Inputs(0U, 1U);
            INST(15U, Opcode::Mul).u64().Inputs(14U, 2U);
        }
        BASIC_BLOCK(7U, -1L)
        {
            INST(17U, Opcode::Phi).u64().Inputs(6U, 15U);
            INST(18U, Opcode::Return).u64().Inputs(17U);
        }
    }
    if constexpr (DOM_RESULT == DominantCondResult::TRUE) {
        BB(2U).SwapTrueFalseSuccessors();
    }
    if constexpr (SWAP_INPUTS == SwapInputs::TRUE) {
        INS(19U).SwapInputs();
    }
}

template <DominantCondResult DOM_RESULT, RemainedSuccessor REMAINED_SUCC, SwapInputs SWAP_INPUTS>
void BranchEliminationTest::BuildGraphAndCheckElimination(ConditionCode dominant_code, ConditionCode code)
{
    auto graph = CreateEmptyGraph();
    BuildContitionsCheckGraph<DOM_RESULT, SWAP_INPUTS>(graph, dominant_code, code);
    auto expected_graph = CreateEmptyGraph();
    if constexpr (REMAINED_SUCC == RemainedSuccessor::FALSE_SUCCESSOR) {
        BuildContitionsCheckGraphElimTrueSucc<DOM_RESULT, SWAP_INPUTS>(expected_graph, dominant_code, code);
    } else if constexpr (REMAINED_SUCC == RemainedSuccessor::TRUE_SUCCESSOR) {
        BuildContitionsCheckGraphElimFalseSucc<DOM_RESULT, SWAP_INPUTS>(expected_graph, dominant_code, code);
    } else {
        BuildContitionsCheckGraph<DOM_RESULT, SWAP_INPUTS>(expected_graph, dominant_code, code);
    }

    graph->RunPass<BranchElimination>();
    ASSERT_TRUE(GraphComparator().Compare(graph, expected_graph));
}

TEST_F(BranchEliminationTest, EliminateByDominatedCondition)
{
    // dominant condition:  a op1 b,
    // dominated condition: a op2 b (reached from false successor)
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::TRUE_SUCCESSOR>(ConditionCode::CC_EQ,
                                                                                               ConditionCode::CC_EQ);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::FALSE_SUCCESSOR>(ConditionCode::CC_EQ,
                                                                                                ConditionCode::CC_NE);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::FALSE_SUCCESSOR>(ConditionCode::CC_EQ,
                                                                                                ConditionCode::CC_LT);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::TRUE_SUCCESSOR>(ConditionCode::CC_EQ,
                                                                                               ConditionCode::CC_LE);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::FALSE_SUCCESSOR>(ConditionCode::CC_EQ,
                                                                                                ConditionCode::CC_GT);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::TRUE_SUCCESSOR>(ConditionCode::CC_EQ,
                                                                                               ConditionCode::CC_GE);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::FALSE_SUCCESSOR>(ConditionCode::CC_EQ,
                                                                                                ConditionCode::CC_B);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::TRUE_SUCCESSOR>(ConditionCode::CC_EQ,
                                                                                               ConditionCode::CC_BE);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::FALSE_SUCCESSOR>(ConditionCode::CC_EQ,
                                                                                                ConditionCode::CC_A);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::TRUE_SUCCESSOR>(ConditionCode::CC_EQ,
                                                                                               ConditionCode::CC_AE);

    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::FALSE_SUCCESSOR>(ConditionCode::CC_EQ,
                                                                                                 ConditionCode::CC_EQ);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::TRUE_SUCCESSOR>(ConditionCode::CC_EQ,
                                                                                                ConditionCode::CC_NE);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_EQ,
                                                                                      ConditionCode::CC_LT);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_EQ,
                                                                                      ConditionCode::CC_LE);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_EQ,
                                                                                      ConditionCode::CC_GT);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_EQ,
                                                                                      ConditionCode::CC_GE);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_EQ,
                                                                                      ConditionCode::CC_B);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_EQ,
                                                                                      ConditionCode::CC_BE);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_EQ,
                                                                                      ConditionCode::CC_A);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_EQ,
                                                                                      ConditionCode::CC_AE);

    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::FALSE_SUCCESSOR>(ConditionCode::CC_LT,
                                                                                                ConditionCode::CC_EQ);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::TRUE_SUCCESSOR>(ConditionCode::CC_LT,
                                                                                               ConditionCode::CC_NE);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::TRUE_SUCCESSOR>(ConditionCode::CC_LT,
                                                                                               ConditionCode::CC_LT);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::TRUE_SUCCESSOR>(ConditionCode::CC_LT,
                                                                                               ConditionCode::CC_LE);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::FALSE_SUCCESSOR>(ConditionCode::CC_LT,
                                                                                                ConditionCode::CC_GT);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::FALSE_SUCCESSOR>(ConditionCode::CC_LT,
                                                                                                ConditionCode::CC_GE);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::BOTH>(ConditionCode::CC_LT,
                                                                                     ConditionCode::CC_B);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::BOTH>(ConditionCode::CC_LT,
                                                                                     ConditionCode::CC_BE);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::BOTH>(ConditionCode::CC_LT,
                                                                                     ConditionCode::CC_A);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::BOTH>(ConditionCode::CC_LT,
                                                                                     ConditionCode::CC_AE);

    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_LT,
                                                                                      ConditionCode::CC_EQ);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_LT,
                                                                                      ConditionCode::CC_NE);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::FALSE_SUCCESSOR>(ConditionCode::CC_LT,
                                                                                                 ConditionCode::CC_LT);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_LT,
                                                                                      ConditionCode::CC_LE);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_LT,
                                                                                      ConditionCode::CC_GT);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::TRUE_SUCCESSOR>(ConditionCode::CC_LT,
                                                                                                ConditionCode::CC_GE);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_LT,
                                                                                      ConditionCode::CC_B);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_LT,
                                                                                      ConditionCode::CC_BE);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_LT,
                                                                                      ConditionCode::CC_A);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_LT,
                                                                                      ConditionCode::CC_AE);

    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::BOTH>(ConditionCode::CC_LE,
                                                                                     ConditionCode::CC_EQ);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::BOTH>(ConditionCode::CC_LE,
                                                                                     ConditionCode::CC_NE);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::BOTH>(ConditionCode::CC_LE,
                                                                                     ConditionCode::CC_LT);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::TRUE_SUCCESSOR>(ConditionCode::CC_LE,
                                                                                               ConditionCode::CC_LE);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::FALSE_SUCCESSOR>(ConditionCode::CC_LE,
                                                                                                ConditionCode::CC_GT);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::BOTH>(ConditionCode::CC_LE,
                                                                                     ConditionCode::CC_GE);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::BOTH>(ConditionCode::CC_LE,
                                                                                     ConditionCode::CC_B);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::BOTH>(ConditionCode::CC_LE,
                                                                                     ConditionCode::CC_BE);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::BOTH>(ConditionCode::CC_LE,
                                                                                     ConditionCode::CC_A);
    BuildGraphAndCheckElimination<DominantCondResult::TRUE, RemainedSuccessor::BOTH>(ConditionCode::CC_LE,
                                                                                     ConditionCode::CC_AE);

    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::FALSE_SUCCESSOR>(ConditionCode::CC_LE,
                                                                                                 ConditionCode::CC_EQ);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::TRUE_SUCCESSOR>(ConditionCode::CC_LE,
                                                                                                ConditionCode::CC_NE);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::FALSE_SUCCESSOR>(ConditionCode::CC_LE,
                                                                                                 ConditionCode::CC_LT);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::FALSE_SUCCESSOR>(ConditionCode::CC_LE,
                                                                                                 ConditionCode::CC_LE);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::TRUE_SUCCESSOR>(ConditionCode::CC_LE,
                                                                                                ConditionCode::CC_GT);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::TRUE_SUCCESSOR>(ConditionCode::CC_LE,
                                                                                                ConditionCode::CC_GE);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_LE,
                                                                                      ConditionCode::CC_B);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_LE,
                                                                                      ConditionCode::CC_BE);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_LE,
                                                                                      ConditionCode::CC_A);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH>(ConditionCode::CC_LE,
                                                                                      ConditionCode::CC_AE);

    // dominant condition:  b op1 a,
    // dominated condition: a op2 b (reached from false successor)
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH, SwapInputs::TRUE>(
        ConditionCode::CC_GT, ConditionCode::CC_EQ);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::FALSE_SUCCESSOR, SwapInputs::TRUE>(
        ConditionCode::CC_GE, ConditionCode::CC_EQ);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::BOTH, SwapInputs::TRUE>(
        ConditionCode::CC_LT, ConditionCode::CC_EQ);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::FALSE_SUCCESSOR, SwapInputs::TRUE>(
        ConditionCode::CC_LE, ConditionCode::CC_EQ);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::FALSE_SUCCESSOR, SwapInputs::TRUE>(
        ConditionCode::CC_EQ, ConditionCode::CC_EQ);
    BuildGraphAndCheckElimination<DominantCondResult::FALSE, RemainedSuccessor::TRUE_SUCCESSOR, SwapInputs::TRUE>(
        ConditionCode::CC_NE, ConditionCode::CC_EQ);
}

TEST_F(BranchEliminationTest, CascadeElimination)
{
    /*
     * Case 1
     *
     *             [0]
     *          T   |  F
     *        /----[2]----\
     *        |           |
     *        v        T  v  F
     *       [3]    /----[4]----\
     *        |     |       T   |  F
     *        |    [5]    /----[6]----\
     *        |     |     |           |
     *        |     |    [7]         [8]
     *        v     v     v           v
     *      [exit]<-------------------/
     *
     *  ---->
     *
     *             [0]
     *              |
     *        /----[2]----\
     *        |           |
     *        v           v
     *       [3]         [4]
     *        |           |
     *        |          [6]
     *        |           |
     *        |          [8]
     *        v           |
     *      [exit]<-------/
     */

    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(3U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(3U);
        }
        BASIC_BLOCK(3U, 9U)
        {
            INST(5U, Opcode::Add).u64().Inputs(0U, 2U);
        }
        BASIC_BLOCK(4U, 5U, 6U)
        {
            INST(7U, Opcode::Compare).b().CC(CC_EQ).Inputs(1U, 0U);
            INST(8U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(7U);
        }
        BASIC_BLOCK(5U, 9U)
        {
            INST(9U, Opcode::Sub).u64().Inputs(0U, 2U);
        }
        BASIC_BLOCK(6U, 7U, 8U)
        {
            INST(11U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
            INST(12U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(11U);
        }
        BASIC_BLOCK(7U, 9U)
        {
            INST(13U, Opcode::Mul).u64().Inputs(0U, 2U);
        }
        BASIC_BLOCK(8U, 9U)
        {
            INST(15U, Opcode::Div).u64().Inputs(0U, 2U);
        }
        BASIC_BLOCK(9U, -1L)
        {
            INST(17U, Opcode::Phi).u64().Inputs(5U, 9U, 13U, 15U);
            INST(18U, Opcode::Return).u64().Inputs(17U);
        }
    }
    graph->RunPass<BranchElimination>();
    graph->RunPass<Cleanup>();

    auto expected_graph = CreateEmptyGraph();
    GRAPH(expected_graph)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();

        BASIC_BLOCK(2U, 3U, 8U)
        {
            INST(3U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(3U);
        }
        BASIC_BLOCK(3U, 9U)
        {
            INST(5U, Opcode::Add).u64().Inputs(0U, 2U);
        }
        BASIC_BLOCK(8U, 9U)
        {
            INST(15U, Opcode::Div).u64().Inputs(0U, 2U);
        }
        BASIC_BLOCK(9U, -1L)
        {
            INST(17U, Opcode::Phi).u64().Inputs(5U, 15U);
            INST(18U, Opcode::Return).u64().Inputs(17U);
        }
    }
    ASSERT_TRUE(GraphComparator().Compare(graph, expected_graph));

    /*
     * Case 2
     *
     *             [0]
     *          F   |  T
     *        /----[2]----\
     *        |           |
     *        v        T  v  F
     *       [3]    /----[4]----\
     *        |     |       T   |  F
     *        |    [5]    /----[6]----\
     *        |     |     |           |
     *        |     |    [7]         [8]
     *        v     v     v           v
     *      [exit]<-------------------/
     *
     * ---->
     *
     *             [0]
     *          T   |  F
     *        /----[2]----\
     *        |           |
     *        v           v
     *       [3]         [4]
     *        |           |
     *        |          [5]
     *        |           |
     *        |           |
     *        v           |
     *      [exit]<-------/
     *
     */
    auto graph_case2 = CreateEmptyGraph();
    GRAPH(graph_case2)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();

        BASIC_BLOCK(2U, 4U, 3U)
        {
            INST(3U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(3U);
        }
        BASIC_BLOCK(3U, 9U)
        {
            INST(5U, Opcode::Add).u64().Inputs(0U, 2U);
        }
        BASIC_BLOCK(4U, 5U, 6U)
        {
            INST(7U, Opcode::Compare).b().CC(CC_EQ).Inputs(1U, 0U);
            INST(8U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(7U);
        }
        BASIC_BLOCK(5U, 9U)
        {
            INST(9U, Opcode::Sub).u64().Inputs(0U, 2U);
        }
        BASIC_BLOCK(6U, 7U, 8U)
        {
            INST(11U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
            INST(12U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(11U);
        }
        BASIC_BLOCK(7U, 9U)
        {
            INST(13U, Opcode::Mul).u64().Inputs(0U, 2U);
        }
        BASIC_BLOCK(8U, 9U)
        {
            INST(15U, Opcode::Div).u64().Inputs(0U, 2U);
        }
        BASIC_BLOCK(9U, -1L)
        {
            INST(17U, Opcode::Phi).u64().Inputs(5U, 9U, 13U, 15U);
            INST(18U, Opcode::Return).u64().Inputs(17U);
        }
    }
    graph_case2->RunPass<BranchElimination>();
    graph_case2->RunPass<Cleanup>();

    auto expected_graph2 = CreateEmptyGraph();
    GRAPH(expected_graph2)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();

        BASIC_BLOCK(2U, 5U, 3U)
        {
            INST(3U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(3U);
        }
        BASIC_BLOCK(3U, 9U)
        {
            INST(5U, Opcode::Add).u64().Inputs(0U, 2U);
        }
        BASIC_BLOCK(5U, 9U)
        {
            INST(9U, Opcode::Sub).u64().Inputs(0U, 2U);
        }
        BASIC_BLOCK(9U, -1L)
        {
            INST(17U, Opcode::Phi).u64().Inputs(5U, 9U);
            INST(18U, Opcode::Return).u64().Inputs(17U);
        }
    }
    ASSERT_TRUE(GraphComparator().Compare(graph_case2, expected_graph2));
}

TEST_F(BranchEliminationTest, ConditionEliminationNotApplied)
{
    /*
     * Case 1:
     *             [0]
     *              |
     *        /----[2]----\
     *        |           |
     *        v           v
     *       [3]         [4]
     *        |           |
     *        \-----------/
     *              |
     *        /----[6]----\
     *        |           |
     *       [7]         [8]
     *        |           |
     *        v           |
     *      [exit]<-------/
     */
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(3U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(3U);
        }
        BASIC_BLOCK(3U, 6U)
        {
            INST(5U, Opcode::Add).u64().Inputs(1U, 2U);
        }
        BASIC_BLOCK(4U, 6U)
        {
            INST(7U, Opcode::Sub).u64().Inputs(1U, 2U);
        }
        BASIC_BLOCK(6U, 7U, 8U)
        {
            INST(9U, Opcode::Phi).Inputs(5U, 7U).u64();
            INST(10U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
            INST(11U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(10U);
        }
        BASIC_BLOCK(7U, 9U)
        {
            INST(12U, Opcode::Add).u64().Inputs(9U, 1U);
        }
        BASIC_BLOCK(8U, 9U)
        {
            INST(14U, Opcode::Sub).u64().Inputs(9U, 1U);
        }
        BASIC_BLOCK(9U, -1L)
        {
            INST(16U, Opcode::Phi).u64().Inputs(12U, 14U);
            INST(17U, Opcode::Return).u64().Inputs(16U);
        }
    }
    graph->RunPass<BranchElimination>();
    EXPECT_EQ(BB(6U).GetSuccsBlocks().size(), 2U);

    /*
     * Case 2
     *             [0]
     *              |
     *        /----[2]----\
     *        |           |
     *        |           |
     *       [3]--------->|
     *                    v
     *              /----[4]----\
     *              |           |
     *             [5]         [6]
     *              |           |
     *              v           |
     *            [exit]<-------/
     */
    auto graph2 = CreateEmptyGraph();
    GRAPH(graph2)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(3U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(3U);
        }
        BASIC_BLOCK(3U, 4U)
        {
            INST(5U, Opcode::Add).u64().Inputs(1U, 2U);
        }
        BASIC_BLOCK(4U, 5U, 6U)
        {
            INST(7U, Opcode::Phi).Inputs(1U, 5U).u64();
            INST(8U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
            INST(9U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(8U);
        }
        BASIC_BLOCK(5U, 7U)
        {
            INST(10U, Opcode::Add).u64().Inputs(7U, 1U);
        }
        BASIC_BLOCK(6U, 7U)
        {
            INST(12U, Opcode::Sub).u64().Inputs(7U, 1U);
        }
        BASIC_BLOCK(7U, -1L)
        {
            INST(14U, Opcode::Phi).u64().Inputs(10U, 12U);
            INST(15U, Opcode::Return).u64().Inputs(14U);
        }
    }
    graph2->RunPass<BranchElimination>();
    EXPECT_EQ(BB(4U).GetSuccsBlocks().size(), 2U);
}

/*
 *             [0]
 *              |
 *        /----[2]----\
 *        |           |
 *        |           v
 *        |          [3]
 *        |           |
 *        |           v
 *        \--------->[4]
 *                    |
 *                    v
 *              /----[5]----\
 *              |           |
 *             [6]         [7]
 *              |           |
 *              v           |
 *            [exit]<-------/
 *
 */
TEST_F(BranchEliminationTest, DomBothSuccessorsReachTargetBlock)
{
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(3U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(3U);
        }
        BASIC_BLOCK(3U, 4U)
        {
            INST(5U, Opcode::Add).u64().Inputs(0U, 0U);
        }
        BASIC_BLOCK(4U, 6U, 7U)
        {
            INST(7U, Opcode::Phi).u64().Inputs({{2U, 0U}, {3U, 5U}});
            INST(8U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
            INST(9U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(8U);
        }
        BASIC_BLOCK(6U, 8U)
        {
            INST(10U, Opcode::Add).u64().Inputs(7U, 1U);
        }
        BASIC_BLOCK(7U, 8U)
        {
            INST(12U, Opcode::Sub).u64().Inputs(7U, 1U);
        }
        BASIC_BLOCK(8U, -1L)
        {
            INST(14U, Opcode::Phi).u64().Inputs(10U, 12U);
            INST(15U, Opcode::Return).u64().Inputs(14U);
        }
    }
    graph->RunPass<BranchElimination>();
    // Elimination NOT applied
    EXPECT_EQ(BB(4U).GetGraph(), graph);
    EXPECT_EQ(BB(4U).GetSuccsBlocks().size(), 2U);
}

TEST_F(BranchEliminationTest, CreateInfiniteLoop)
{
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        CONSTANT(1U, 1U);

        BASIC_BLOCK(2U, 2U, 3U)
        {
            INST(2U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(1U);
        }
        BASIC_BLOCK(3U, -1L)
        {
            INST(4U, Opcode::ReturnVoid);
        }
    }
    ASSERT_TRUE(graph->HasEndBlock());
    graph->RunPass<BranchElimination>();
    ASSERT_FALSE(graph->HasEndBlock());

    auto expected_graph = CreateEmptyGraph();
    GRAPH(expected_graph)
    {
        CONSTANT(1U, 1U);
        BASIC_BLOCK(2U, 3U)
        {  // pre-header
        }
        BASIC_BLOCK(3U, 3U)
        {  // infinite loop
        }
    }
    ASSERT_TRUE(GraphComparator().Compare(graph, expected_graph));
}

/**
 *          compare1
 *            ...
 *        if_imm(compare1)
 */
TEST_F(BranchEliminationTest, CompareAndIfNotSameBlock)
{
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        PARAMETER(0U, 0U).s64();
        PARAMETER(1U, 1U).s64();
        PARAMETER(2U, 2U).s64();
        CONSTANT(3U, 0U);
        BASIC_BLOCK(2U, 3U)
        {
            INST(4U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
        }
        BASIC_BLOCK(3U, 4U, 5U)
        {
            INST(5U, Opcode::Phi).s64().Inputs(2U, 8U);
            INST(6U, Opcode::Compare).b().CC(CC_LT).Inputs(5U, 0U);
            INST(7U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(6U);
        }
        BASIC_BLOCK(4U, 3U)
        {
            INST(8U, Opcode::Add).s64().Inputs(5U, 3U);
        }
        BASIC_BLOCK(5U, 6U, 7U)
        {
            INST(9U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(4U);
        }  // if INS(4) != 0 goto BB(6) else goto BB(7)
        BASIC_BLOCK(6U, 12U)
        {
            INST(10U, Opcode::Mul).u64().Inputs(0U, 1U);
        }
        BASIC_BLOCK(7U, 8U, 9U)
        {
            INST(11U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);  // equal to INS(4) -> INS(11) == 0
            INST(12U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_EQ).Imm(0U).Inputs(11U);
        }  // INS(11) == 0 is true -> goto BB(8), remove BB(9)
        BASIC_BLOCK(8U, 10U, 11U)
        {
            INST(14U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(11U);
        }  // INS(11) == 1 is false -> goto BB(11), remove BB(10)
        BASIC_BLOCK(9U, 12U)
        {
            INST(15U, Opcode::Sub).u64().Inputs(0U, 1U);
        }
        BASIC_BLOCK(10U, 12U)
        {
            INST(16U, Opcode::Mul).u64().Inputs(1U, 1U);
        }
        BASIC_BLOCK(11U, 12U)
        {
            INST(17U, Opcode::Add).u64().Inputs(2U, 2U);
        }
        BASIC_BLOCK(12U, -1L)
        {
            INST(18U, Opcode::Phi).u64().Inputs(10U, 15U, 16U, 17U);
            INST(19U, Opcode::Return).u64().Inputs(18U);
        }
    }
    graph->RunPass<BranchElimination>();
    graph->RunPass<Cleanup>();

    auto expected_graph = CreateEmptyGraph();
    GRAPH(expected_graph)
    {
        PARAMETER(0U, 0U).s64();
        PARAMETER(1U, 1U).s64();
        PARAMETER(2U, 2U).s64();
        CONSTANT(3U, 0U);

        BASIC_BLOCK(2U, 3U)
        {
            INST(4U, Opcode::Compare).b().CC(CC_EQ).Inputs(0U, 1U);
        }
        BASIC_BLOCK(3U, 4U, 5U)
        {
            INST(5U, Opcode::Phi).s64().Inputs(2U, 8U);
            INST(6U, Opcode::Compare).b().CC(CC_LT).Inputs(5U, 0U);
            INST(7U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(6U);
        }
        BASIC_BLOCK(4U, 3U)
        {
            INST(8U, Opcode::Add).s64().Inputs(5U, 3U);
        }
        BASIC_BLOCK(5U, 6U, 11U)
        {
            INST(9U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(4U);
        }
        BASIC_BLOCK(6U, 12U)
        {
            INST(10U, Opcode::Mul).u64().Inputs(0U, 1U);
        }
        BASIC_BLOCK(11U, 12U)
        {
            INST(17U, Opcode::Add).u64().Inputs(2U, 2U);
        }
        BASIC_BLOCK(12U, -1L)
        {
            INST(18U, Opcode::Phi).u64().Inputs(10U, 17U);
            INST(19U, Opcode::Return).u64().Inputs(18U);
        }
    }
    ASSERT_TRUE(GraphComparator().Compare(graph, expected_graph));
}

TEST_F(BranchEliminationTest, DisconnectPhiWithInputItself)
{
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        CONSTANT(0U, 0U);
        CONSTANT(1U, 1U);
        CONSTANT(2U, 10U);
        PARAMETER(3U, 0U).s64();
        BASIC_BLOCK(2U, 10U, 4U)
        {
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_EQ).Imm(0U).Inputs(0U);
        }
        BASIC_BLOCK(4U, 7U)
        {
            INST(5U, Opcode::Mul).s64().Inputs(2U, 2U);
        }
        BASIC_BLOCK(7U, 8U, 10U)
        {
            INST(6U, Opcode::Phi).s64().Inputs(0U, 12U, 13U);
            INST(7U, Opcode::Phi).s64().Inputs(5U, 6U, 7U);
            INST(8U, Opcode::Compare).b().CC(CC_LT).Inputs(6U, 7U);
            INST(9U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(8U);
        }
        BASIC_BLOCK(8U, 5U, 6U)
        {
            INST(10U, Opcode::Compare).b().CC(CC_LT).Inputs(6U, 3U);
            INST(11U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(10U);
        }
        BASIC_BLOCK(5U, 7U)
        {
            INST(12U, Opcode::Add).s64().Inputs(6U, 1U);
        }
        BASIC_BLOCK(6U, 7U)
        {
            INST(13U, Opcode::Add).s64().Inputs(6U, 2U);
        }
        BASIC_BLOCK(10U, -1L)
        {
            INST(20U, Opcode::Phi).s64().Inputs(0U, 6U);
            INST(21U, Opcode::Return).s64().Inputs(20U);
        }
    }
    graph->RunPass<BranchElimination>();
    graph->RunPass<Cleanup>();

    auto expected_graph = CreateEmptyGraph();
    GRAPH(expected_graph)
    {
        CONSTANT(0U, 0U);
        BASIC_BLOCK(2U, -1L)
        {
            INST(21U, Opcode::Return).s64().Inputs(0U);
        }
    }
    ASSERT_TRUE(GraphComparator().Compare(graph, expected_graph));
}
// NOLINTEND(readability-magic-numbers)

}  // namespace panda::compiler
