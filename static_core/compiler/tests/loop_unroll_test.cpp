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
#include "optimizer/optimizations/regalloc/reg_alloc.h"
#include "optimizer/optimizations/loop_unroll.h"
#include "optimizer/optimizations/cleanup.h"
#include "optimizer/code_generator/codegen.h"
#include "optimizer/ir/graph_cloner.h"

#if defined(PANDA_TARGET_ARM64) || defined(PANDA_TARGET_AMD64)
#include "vixl_exec_module.h"
#endif

namespace panda::compiler {
// NOLINTBEGIN(readability-magic-numbers)
class LoopUnrollTest : public GraphTest {
public:
#if defined(PANDA_TARGET_ARM64) || defined(PANDA_TARGET_AMD64)
    LoopUnrollTest() : opcodes_count_(GetAllocator()->Adapter()), exec_module_(GetAllocator(), GetGraph()->GetRuntime())
#else
    LoopUnrollTest() : opcodes_count_(GetAllocator()->Adapter())
#endif
    {
    }

    template <typename T, typename... Args>
    bool CheckRetOnVixlSimulator([[maybe_unused]] Graph *graph, [[maybe_unused]] T return_value,
                                 [[maybe_unused]] Args... args)
    {
#if defined(PANDA_TARGET_ARM64) || defined(PANDA_TARGET_AMD64)
#ifndef NDEBUG
        // GraphChecker hack: LowLevel instructions may appear only after Lowering pass:
        graph->SetLowLevelInstructionsEnabled();
#endif
        graph->RunPass<Cleanup>();
        EXPECT_TRUE(RegAlloc(graph));
        EXPECT_TRUE(graph->RunPass<Codegen>());
        auto entry = reinterpret_cast<char *>(graph->GetCode().Data());
        auto exit = entry + graph->GetCode().Size();  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        ASSERT(entry != nullptr && exit != nullptr);
        exec_module_.SetInstructions(entry, exit);
        exec_module_.SetParameters(args...);
        exec_module_.Execute();
        return exec_module_.GetRetValue<T>() == return_value;
#else
        return true;
#endif
    }

    size_t CountOpcodes(const ArenaVector<BasicBlock *> &blocks)
    {
        size_t count_inst = 0;
        opcodes_count_.clear();
        for (auto block : blocks) {
            for (auto inst : block->AllInsts()) {
                opcodes_count_[inst->GetOpcode()]++;
                count_inst++;
            }
        }
        return count_inst;
    }

    size_t GetOpcodeCount(Opcode opcode)
    {
        return opcodes_count_.at(opcode);
    }

    void CheckSimpleLoop(uint32_t inst_limit, uint32_t unroll_factor, uint32_t expected_factor)
    {
        auto graph = CreateEmptyGraph();
        GRAPH(graph)
        {
            PARAMETER(0, 0).u64();    // a = 0
            PARAMETER(1, 1).u64();    // b = 1
            PARAMETER(2, 100).u64();  // c = 100
            PARAMETER(3, 101).u64();

            BASIC_BLOCK(2, 2, 3)
            {
                INST(4, Opcode::Phi).u64().Inputs(0, 6);
                INST(5, Opcode::Phi).u64().Inputs(1, 7);
                INST(6, Opcode::Mul).u64().Inputs(4, 4);  // a = a * a
                INST(7, Opcode::Add).u64().Inputs(5, 3);  // b = b + 1

                INST(8, Opcode::Compare).CC(CC_LT).b().Inputs(7, 2);  // while b < c
                INST(9, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(8);
            }
            BASIC_BLOCK(3, -1)
            {
                INST(10, Opcode::Sub).u64().Inputs(6, 7);
                INST(11, Opcode::Return).u64().Inputs(10);  // return (a - b)
            }
        }
        graph->RunPass<LoopUnroll>(inst_limit, unroll_factor);
        graph->RunPass<Cleanup>();

        // Check number of instructions
        CountOpcodes(graph->GetBlocksRPO());
        EXPECT_EQ(GetOpcodeCount(Opcode::Add), expected_factor);
        EXPECT_EQ(GetOpcodeCount(Opcode::Mul), expected_factor);
        EXPECT_EQ(GetOpcodeCount(Opcode::Compare), expected_factor);
        EXPECT_EQ(GetOpcodeCount(Opcode::IfImm), expected_factor);
        EXPECT_EQ(GetOpcodeCount(Opcode::Sub), 1U);
        EXPECT_EQ(GetOpcodeCount(Opcode::Parameter), 4U);

        if (expected_factor > 1) {
            // Check control-flow
            EXPECT_EQ(BB(3).GetSuccsBlocks().size(), 1U);
            EXPECT_EQ(BB(3).GetSuccessor(0), graph->GetEndBlock());
            EXPECT_EQ(BB(3).GetPredsBlocks().size(), expected_factor);

            // phi1 [INST(6, Mul), INST(6', Mul), INST(6'', Mul)]
            auto phi1 = INS(10).GetInput(0).GetInst();
            EXPECT_TRUE(phi1->IsPhi() && phi1->GetInputsCount() == expected_factor);
            EXPECT_TRUE(phi1->GetInput(0) == &INS(6));
            for (auto input : phi1->GetInputs()) {
                EXPECT_TRUE(input.GetInst()->GetOpcode() == Opcode::Mul);
            }

            // phi2 [INST(7, Add), INST(7', Add), INST(7'', Add)]
            auto phi2 = INS(10).GetInput(1).GetInst();
            EXPECT_TRUE(phi2->IsPhi() && phi2->GetInputsCount() == expected_factor);
            EXPECT_TRUE(phi2->GetInput(0) == &INS(7));
            for (auto input : phi2->GetInputs()) {
                EXPECT_TRUE(input.GetInst()->GetOpcode() == Opcode::Add);
            }

            // Check cloned `Mul` instruction inputs
            for (size_t i = 1; i < phi1->GetInputsCount(); i++) {
                auto cloned_mul = phi1->GetInput(i).GetInst();
                auto prev_mul = phi1->GetInput(i - 1).GetInst();
                EXPECT_TRUE(cloned_mul->GetInput(0) == prev_mul);
                EXPECT_TRUE(cloned_mul->GetInput(1) == prev_mul);
            }

            // Check cloned `Add` instruction inputs
            for (size_t i = 1; i < phi2->GetInputsCount(); i++) {
                auto cloned_add = phi2->GetInput(i).GetInst();
                auto prev_add = phi2->GetInput(i - 1).GetInst();
                EXPECT_TRUE(cloned_add->GetInput(0) == prev_add);
                EXPECT_TRUE(cloned_add->GetInput(1) == &INS(3));
            }
        } else {
            EXPECT_EQ(INS(10).GetInput(0).GetInst(), &INS(6));
            EXPECT_EQ(INS(10).GetInput(1).GetInst(), &INS(7));
            EXPECT_EQ(BB(3).GetPredsBlocks().size(), 1U);
            EXPECT_EQ(BB(3).GetPredsBlocks()[0], &BB(2));
        }
    }

    void CheckLoopWithPhiAndSafePoint(uint32_t inst_limit, uint32_t unroll_factor, uint32_t expected_factor)
    {
        auto graph = CreateEmptyGraph();
        GRAPH(graph)
        {
            PARAMETER(0, 0).u64();  // a = 26
            PARAMETER(1, 1).u64();  // b = 0
            CONSTANT(2, 0);         // const 0
            CONSTANT(3, 1UL);       // const 1
            CONSTANT(4, 2UL);       // const 2
            CONSTANT(5, 10UL);      // const 10

            BASIC_BLOCK(2, 3, 4)
            {
                INST(6, Opcode::Phi).u64().Inputs(0, 15);
                INST(7, Opcode::Phi).u64().Inputs(1, 14);
                INST(20, Opcode::SafePoint).Inputs(0, 1).SrcVregs({0, 1});
                INST(8, Opcode::Mod).u64().Inputs(6, 4);              // mod = a % 2
                INST(9, Opcode::Compare).CC(CC_EQ).b().Inputs(8, 3);  // if mod == 1
                INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(9);
            }
            BASIC_BLOCK(3, 5)
            {
                INST(11, Opcode::Add).u64().Inputs(7, 2);  // b = b + 0
            }
            BASIC_BLOCK(4, 5)
            {
                INST(12, Opcode::Sub).u64().Inputs(7, 3);  // b = b + 1
            }
            BASIC_BLOCK(5, 6, 2)
            {
                INST(13, Opcode::Phi).u64().Inputs(11, 12);
                INST(14, Opcode::Mul).u64().Inputs(13, 5);             // b = b * 10
                INST(15, Opcode::Div).u64().Inputs(6, 4);              // a = a / 2
                INST(16, Opcode::Compare).CC(CC_EQ).b().Inputs(6, 2);  // if a = 0
                INST(17, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(16);
            }
            BASIC_BLOCK(6, -1)
            {
                INST(18, Opcode::Div).u64().Inputs(14, 5);  // b = b / 10
                INST(19, Opcode::Return).u64().Inputs(18);  // return b
            }
        }
        graph->RunPass<LoopUnroll>(inst_limit, unroll_factor);
        GraphChecker(graph).Check();

        // Check number of instructions
        CountOpcodes(graph->GetBlocksRPO());
        EXPECT_EQ(GetOpcodeCount(Opcode::Add), expected_factor);
        EXPECT_EQ(GetOpcodeCount(Opcode::Sub), expected_factor);
        EXPECT_EQ(GetOpcodeCount(Opcode::Mul), expected_factor);
        EXPECT_EQ(GetOpcodeCount(Opcode::Mod), expected_factor);
        EXPECT_EQ(GetOpcodeCount(Opcode::Div), expected_factor + 1);
        EXPECT_EQ(GetOpcodeCount(Opcode::IfImm), 2 * expected_factor);
        EXPECT_EQ(GetOpcodeCount(Opcode::Compare), 2 * expected_factor);
        size_t extra_phi = (expected_factor > 1) ? 1 : 0;
        EXPECT_EQ(GetOpcodeCount(Opcode::Phi),
                  2 + expected_factor + extra_phi);        // 2 in the front-block + N unrolled + 1 in the outer-block
        EXPECT_EQ(GetOpcodeCount(Opcode::SafePoint), 1U);  // SafePoint isn't unrolled

        if (expected_factor > 1) {
            // Check control-flow
            auto outer_block = BB(5).GetTrueSuccessor();
            EXPECT_EQ(outer_block->GetSuccsBlocks().size(), 1U);
            EXPECT_EQ(outer_block->GetSuccessor(0), &BB(6));
            EXPECT_EQ(outer_block->GetPredsBlocks().size(), expected_factor);

            // phi [INST(14, Mul), INST(14', Mul)]
            auto phi = INS(18).GetInput(0).GetInst();
            EXPECT_TRUE(phi->IsPhi() && phi->GetInputsCount() == expected_factor);
            EXPECT_TRUE(phi->GetInput(0) == &INS(14));
            for (auto input : phi->GetInputs()) {
                EXPECT_TRUE(input.GetInst()->GetOpcode() == Opcode::Mul);
            }

            // Check cloned `Mul` instruction inputs
            for (size_t i = 1; i < phi->GetInputsCount(); i++) {
                auto cloned_mul = phi->GetInput(i).GetInst();
                auto prev_mul = phi->GetInput(i - 1).GetInst();
                EXPECT_TRUE(cloned_mul->GetInput(0).GetInst()->IsPhi());
                EXPECT_TRUE(prev_mul->GetInput(0).GetInst()->IsPhi());
                EXPECT_NE(cloned_mul->GetInput(0).GetInst(), prev_mul->GetInput(0).GetInst());
                EXPECT_TRUE(cloned_mul->GetInput(1) == &INS(5));
            }
        } else {
            EXPECT_EQ(INS(18).GetInput(0).GetInst(), &INS(14));
            EXPECT_EQ(INS(18).GetInput(1).GetInst(), &INS(5));
            EXPECT_EQ(BB(6).GetPredsBlocks().size(), 1U);
            EXPECT_EQ(BB(6).GetPredsBlocks()[0], &BB(5));
        }
    }

    Graph *BuildGraphPhiInputOfAnotherPhi();
    Graph *BuildLoopWithIncrement(ConditionCode cc, std::optional<uint64_t> start, uint64_t stop, uint64_t step,
                                  DataType::Type type = DataType::INT32);
    Graph *BuildLoopWithDecrement(ConditionCode cc, std::optional<uint64_t> start, std::optional<uint64_t> stop,
                                  uint64_t step, DataType::Type type = DataType::INT32);

protected:
    static constexpr uint32_t INST_LIMIT = 1000;
    static constexpr uint32_t ZERO_START = 0;
    static constexpr uint32_t ZERO_STOP = 0;

private:
    ArenaUnorderedMap<Opcode, size_t> opcodes_count_;
#if defined(PANDA_TARGET_ARM64) || defined(PANDA_TARGET_AMD64)
    VixlExecModule exec_module_;
#endif
};

/*
 * Test Graph:
 *              [0]
 *               |
 *               v
 *              [2]<----\
 *               |      |
 *               v      |
 *              [3]-----/
 *               |
 *               v
 *             [exit]
 *
 *
 * After unroll with FACTOR = 3
 *
 *              [0]
 *               |
 *               v
 *              [2]<----\
 *               |      |
 *               v      |
 *         /----[3]     |
 *         |     |      |
 *         |     v      |
 *         |    [2']    |
 *         |     |      |
 *         |     v      |
 *         |<---[3']    |
 *         |     |      |
 *         |     v      |
 *         |    [2'']   |
 *         |     |      |
 *         |     v      |
 *         |<---[3'']---/
 *         |
 *         |
 *         \-->[outer]
 *                |
 *                v
 *              [exit]
 *
 */

/**
 * There are 6 instructions in the loop [bb2, bb3], 4 of them are cloneable
 * So we have the following mapping form unroll factor to number on unrolled instructions:
 *
 * factor | unrolled inst count
 * 1        6
 * 2        10
 * 3        14
 * 4        18
 * ...
 * 100      402
 *
 * unrolled_inst_count = (factor * cloneable_inst) + (not_cloneable_inst)
 */
TEST_F(LoopUnrollTest, SimpleLoop)
{
    CheckSimpleLoop(0, 4, 1);
    CheckSimpleLoop(6, 4, 1);
    CheckSimpleLoop(9, 4, 1);
    CheckSimpleLoop(10, 4, 2);
    CheckSimpleLoop(14, 4, 3);
    CheckSimpleLoop(100, 4, 4);
    CheckSimpleLoop(100, 10, 10);
    CheckSimpleLoop(400, 100, 99);
    CheckSimpleLoop(1000, 100, 100);
}

/*
 * Test Graph:
 *              [0]
 *               |
 *               v
 *              [2]<--------\
 *             /   \        |
 *            v     v       |
 *           [3]    [4]     |
 *            \      /      |
 *             v    v       |
 *              [5]---------/
 *               |
 *               v
 *             [exit]
 *
 * After unroll with FACTOR = 2
 *
 *              [0]
 *               |
 *               v
 *              [2]<--------\
 *             /   \        |
 *            v     v       |
 *           [3]    [4]     |
 *            \      /      |
 *             v    v       |
 *  /-----------[5]         |
 *  |            |          |
 *  |            v          |
 *  |           [2']        |
 *  |          /   \        |
 *  |         v     v       |
 *  |       [3']    [4']    |
 *  |         \      /      |
 *  |          v    v       |
 *  |           [5']--------/
 *  |            |
 *  |            v
 *  \--------->[outer]
 *               |
 *               v
 *             [exit]
 */

/**
 * There are 13 instructions in the loop [bb2, bb3, bb4, bb5], 10 of them are cloneable
 * So we have the following mapping form unroll factor to number on unrolled instructions:
 *
 * factor | unrolled inst count
 * 1        13
 * 2        23
 * 3        33
 * 4        43
 * ...
 * 100      1003
 *
 * unrolled_inst_count = (factor * cloneable_inst) + (not_cloneable_inst)
 */
TEST_F(LoopUnrollTest, LoopWithPhisAndSafePoint)
{
    CheckLoopWithPhiAndSafePoint(0, 4, 1);
    CheckLoopWithPhiAndSafePoint(13, 4, 1);
    CheckLoopWithPhiAndSafePoint(22, 4, 1);
    CheckLoopWithPhiAndSafePoint(23, 4, 2);
    CheckLoopWithPhiAndSafePoint(33, 4, 3);
    CheckLoopWithPhiAndSafePoint(100, 4, 4);
    CheckLoopWithPhiAndSafePoint(1000, 10, 10);
    CheckLoopWithPhiAndSafePoint(1003, 100, 100);
}

/*
 * Test Graph:
 *              [0]
 *               |
 *               v
 *         /----[2]<----\
 *         |     |      |
 *         |     v      |
 *         |    [3]-----/
 *         |
 *         |
 *         \--->[4]
 *               |
 *               v
 *       /----->[5]<-----\
 *       |       |       |
 *       |       v       |
 *       \------[6]      |
 *               |       |
 *               v       |
 *              [7]------/
 *               |
 *               v
 *             [exit]
 */
TEST_F(LoopUnrollTest, UnrollNotApplied)
{
    constexpr uint32_t UNROLL_FACTOR = 2;
    GRAPH(GetGraph())
    {
        PARAMETER(0, 0).u64();
        PARAMETER(1, 1).u64();
        PARAMETER(2, 2).u64();
        BASIC_BLOCK(2, 3, 4)
        {
            INST(3, Opcode::Compare).b().Inputs(0, 1);
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(3);
        }
        BASIC_BLOCK(3, 2)
        {
            INST(20, Opcode::SaveState).NoVregs();
            INST(14, Opcode::CallStatic).v0id().InputsAutoType(20);
        }
        BASIC_BLOCK(4, 5) {}
        BASIC_BLOCK(5, 6) {}
        BASIC_BLOCK(6, 7, 5)
        {
            INST(13, Opcode::Add).u64().Inputs(1, 2);
            INST(8, Opcode::Compare).b().Inputs(13, 0);
            INST(9, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(8);
        }
        BASIC_BLOCK(7, 5, 8)
        {
            INST(10, Opcode::Compare).b().Inputs(1, 2);
            INST(11, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(10);
        }
        BASIC_BLOCK(8, -1)
        {
            INST(12, Opcode::ReturnVoid);
        }
    }
    auto inst_count = CountOpcodes(GetGraph()->GetBlocksRPO());
    auto cmp_count = GetOpcodeCount(Opcode::Compare);
    auto if_count = GetOpcodeCount(Opcode::IfImm);
    auto add_count = GetOpcodeCount(Opcode::Add);

    GetGraph()->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    GraphChecker(GetGraph()).Check();
    auto unrolled_count = CountOpcodes(GetGraph()->GetBlocksRPO());
    EXPECT_EQ(GetOpcodeCount(Opcode::Compare), cmp_count);
    EXPECT_EQ(GetOpcodeCount(Opcode::IfImm), if_count);
    EXPECT_EQ(GetOpcodeCount(Opcode::Add), add_count);
    EXPECT_EQ(unrolled_count, inst_count);

    EXPECT_EQ(BB(8).GetPredsBlocks().size(), 1U);
    EXPECT_EQ(BB(8).GetPredsBlocks()[0], &BB(7));
}

/**
 *  a, b, c = 0, 1, 2
 *  while c < 100:
 *      a, b, c = b, c, c + a
 *  return c
 */
Graph *LoopUnrollTest::BuildGraphPhiInputOfAnotherPhi()
{
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 0);
        CONSTANT(1, 1);
        CONSTANT(2, 2);

        BASIC_BLOCK(2, 2, 3)
        {
            INST(4, Opcode::Phi).u64().Inputs(0, 5);
            INST(5, Opcode::Phi).u64().Inputs(1, 6);
            INST(6, Opcode::Phi).u64().Inputs(2, 7);
            INST(7, Opcode::Add).u64().Inputs(4, 6);
            INST(8, Opcode::IfImm).SrcType(DataType::UINT64).CC(CC_LT).Imm(100).Inputs(7);
        }

        BASIC_BLOCK(3, -1)
        {
            INST(10, Opcode::Return).u64().Inputs(7);
        }
    }
    return graph;
}

TEST_F(LoopUnrollTest, PhiInputOfAnotherPhi)
{
    // Test with UNROLL_FACTOR = 2

    auto graph = BuildGraphPhiInputOfAnotherPhi();

    auto graph_unroll_factor_2 = CreateEmptyGraph();
    GRAPH(graph_unroll_factor_2)
    {
        CONSTANT(0, 0);
        CONSTANT(1, 1);
        CONSTANT(2, 2);

        BASIC_BLOCK(2, 4, 3)
        {
            INST(4, Opcode::Phi).u64().Inputs(0, 6);
            INST(5, Opcode::Phi).u64().Inputs(1, 7);
            INST(6, Opcode::Phi).u64().Inputs(2, 11);
            INST(7, Opcode::Add).u64().Inputs(4, 6);
            INST(8, Opcode::IfImm).SrcType(DataType::UINT64).CC(CC_LT).Imm(100).Inputs(7);
        }

        BASIC_BLOCK(4, 2, 3)
        {
            INST(11, Opcode::Add).u64().Inputs(5, 7);
            INST(12, Opcode::IfImm).SrcType(DataType::UINT64).CC(CC_LT).Imm(100).Inputs(11);
        }

        BASIC_BLOCK(3, -1)
        {
            INST(9, Opcode::Phi).u64().Inputs(7, 11);
            INST(10, Opcode::Return).u64().Inputs(9);
        }
    }

    static constexpr uint64_t PROGRAM_RESULT = 101;
    graph->RunPass<LoopUnroll>(INST_LIMIT, 2);
    graph->RunPass<Cleanup>();
    EXPECT_TRUE(GraphComparator().Compare(graph, graph_unroll_factor_2));
    EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph_unroll_factor_2, PROGRAM_RESULT));

    // Test with UNROLL_FACTOR = 4

    graph = BuildGraphPhiInputOfAnotherPhi();

    auto graph_unroll_factor_4 = CreateEmptyGraph();
    GRAPH(graph_unroll_factor_4)
    {
        CONSTANT(0, 0);
        CONSTANT(1, 1);
        CONSTANT(2, 2);

        BASIC_BLOCK(2, 4, 3)
        {
            INST(4, Opcode::Phi).u64().Inputs(0, 11);
            INST(5, Opcode::Phi).u64().Inputs(1, 13);
            INST(6, Opcode::Phi).u64().Inputs(2, 15);
            INST(7, Opcode::Add).u64().Inputs(4, 6);
            INST(8, Opcode::IfImm).SrcType(DataType::UINT64).CC(CC_LT).Imm(100).Inputs(7);
        }

        BASIC_BLOCK(4, 5, 3)
        {
            INST(11, Opcode::Add).u64().Inputs(5, 7);
            INST(12, Opcode::IfImm).SrcType(DataType::UINT64).CC(CC_LT).Imm(100).Inputs(11);
        }

        BASIC_BLOCK(5, 6, 3)
        {
            INST(13, Opcode::Add).u64().Inputs(6, 11);
            INST(14, Opcode::IfImm).SrcType(DataType::UINT64).CC(CC_LT).Imm(100).Inputs(13);
        }

        BASIC_BLOCK(6, 2, 3)
        {
            INST(15, Opcode::Add).u64().Inputs(7, 13);
            INST(16, Opcode::IfImm).SrcType(DataType::UINT64).CC(CC_LT).Imm(100).Inputs(15);
        }

        BASIC_BLOCK(3, -1)
        {
            INST(9, Opcode::Phi).u64().Inputs(7, 11, 13, 15);
            INST(10, Opcode::Return).u64().Inputs(9);
        }
    }

    graph->RunPass<LoopUnroll>(INST_LIMIT, 4);
    graph->RunPass<Cleanup>();
    EXPECT_TRUE(GraphComparator().Compare(graph, graph_unroll_factor_4));
    EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph_unroll_factor_4, PROGRAM_RESULT));
}

TEST_F(LoopUnrollTest, PhiInputsOutsideLoop)
{
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        PARAMETER(0, 0).u64();
        CONSTANT(1, 1);
        CONSTANT(2, 2);

        BASIC_BLOCK(2, 2, 3)
        {
            INST(4, Opcode::Phi).u64().Inputs(1, 2);
            INST(5, Opcode::Phi).u64().Inputs(0, 6);
            INST(6, Opcode::Add).u64().Inputs(4, 5);
            INST(7, Opcode::IfImm).SrcType(DataType::UINT64).CC(CC_LT).Imm(100).Inputs(6);
        }

        BASIC_BLOCK(3, -1)
        {
            INST(10, Opcode::Return).u64().Inputs(5);
        }
    }

    graph->RunPass<LoopUnroll>(INST_LIMIT, 2);
    graph->RunPass<Cleanup>();

    auto expected_graph = CreateEmptyGraph();
    GRAPH(expected_graph)
    {
        PARAMETER(0, 0).u64();
        CONSTANT(1, 1);
        CONSTANT(2, 2);
        BASIC_BLOCK(2, 3)
        {
            // preheader
        }
        BASIC_BLOCK(3, 4, 5)
        {
            INST(4, Opcode::Phi).u64().Inputs(1, 2);
            INST(5, Opcode::Phi).u64().Inputs(0, 8);
            INST(6, Opcode::Add).u64().Inputs(4, 5);
            INST(7, Opcode::IfImm).SrcType(DataType::UINT64).CC(CC_LT).Imm(100).Inputs(6);
        }

        BASIC_BLOCK(4, 3, 5)
        {
            INST(8, Opcode::Add).u64().Inputs(2, 6);
            INST(9, Opcode::IfImm).SrcType(DataType::UINT64).CC(CC_LT).Imm(100).Inputs(8);
        }
        BASIC_BLOCK(5, -1)
        {
            INST(10, Opcode::Phi).u64().Inputs(5, 6);
            INST(11, Opcode::Return).u64().Inputs(10);
        }
    }
    EXPECT_TRUE(GraphComparator().Compare(graph, expected_graph));
}

Graph *LoopUnrollTest::BuildLoopWithIncrement(ConditionCode cc, std::optional<uint64_t> start, uint64_t stop,
                                              uint64_t step, DataType::Type type)
{
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        CONSTANT(0, stop);
        CONSTANT(1, 0);  // b = 0
        CONSTANT(2, step);
        if (start) {
            CONSTANT(13, *start);
        } else {
            PARAMETER(13, 0).SetType(type);  // initial `a` to test unroll with non-const boundaries
        }
        BASIC_BLOCK(2, 3, 4)
        {
            INST(3, Opcode::Compare).b().SrcType(type).CC(cc).Inputs(13, 0);
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(3);  // if a [cc] stop
        }
        BASIC_BLOCK(3, 3, 4)
        {
            INST(5, Opcode::Phi).SetType(type).Inputs(13, 7);  // a
            INST(6, Opcode::Phi).SetType(type).Inputs(1, 8);   // b
            INST(7, Opcode::Add).SetType(type).Inputs(5, 2);   // a += step
            INST(8, Opcode::Add).SetType(type).Inputs(6, 7);   // b += a
            INST(9, Opcode::Compare).b().SrcType(type).CC(cc).Inputs(7, 0);
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(9);  // if a [cc] stop
        }
        BASIC_BLOCK(4, -1)
        {
            INST(11, Opcode::Phi).SetType(type).Inputs(1, 6);
            INST(12, Opcode::Return).SetType(type).Inputs(11);  // return b
        }
    }
    return graph;
}

Graph *LoopUnrollTest::BuildLoopWithDecrement(ConditionCode cc, std::optional<uint64_t> start,
                                              std::optional<uint64_t> stop, uint64_t step, DataType::Type type)
{
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        size_t param = 0;
        if (start) {
            CONSTANT(0, *start);
        } else {
            PARAMETER(0, param++).SetType(type);  // initial `a` to test unroll with non-const boundaries
        }
        if (stop) {
            CONSTANT(13, *stop);
        } else {
            PARAMETER(13, param++).SetType(type);
        }
        CONSTANT(1, 0);  // b = 0
        CONSTANT(2, step);

        BASIC_BLOCK(2, 3, 4)
        {
            INST(3, Opcode::Compare).b().SrcType(type).CC(cc).Inputs(0, 13);  // if a [cc] stop
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(3);
        }
        BASIC_BLOCK(3, 3, 4)
        {
            INST(5, Opcode::Phi).SetType(type).Inputs(0, 8);                  // a
            INST(6, Opcode::Phi).SetType(type).Inputs(1, 7);                  // b
            INST(7, Opcode::Add).SetType(type).Inputs(6, 5);                  // b += a
            INST(8, Opcode::Sub).SetType(type).Inputs(5, 2);                  // a -= 1
            INST(9, Opcode::Compare).b().SrcType(type).CC(cc).Inputs(8, 13);  // if a [cc] stop
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(9);
        }
        BASIC_BLOCK(4, -1)
        {
            INST(11, Opcode::Phi).SetType(type).Inputs(1, 7);
            INST(12, Opcode::Return).SetType(type).Inputs(11);  // return b
        }
    }
    return graph;
}

TEST_F(LoopUnrollTest, ConstCountableLoopWithIncrement)
{
    static constexpr uint32_t INC_STEP = 1;
    static constexpr uint32_t INC_STOP = 10;
    for (size_t unroll_factor = 1; unroll_factor <= 10; unroll_factor++) {
        auto graph = BuildLoopWithIncrement(CC_LT, ZERO_START, INC_STOP, INC_STEP);
        graph->RunPass<LoopUnroll>(INST_LIMIT, unroll_factor);
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, 45));

        graph = BuildLoopWithIncrement(CC_LE, ZERO_START, INC_STOP, INC_STEP);
        graph->RunPass<LoopUnroll>(INST_LIMIT, unroll_factor);
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, 55));
    }

    static constexpr uint32_t UNROLL_FACTOR = 2;
    auto graph = BuildLoopWithIncrement(CC_LT, ZERO_START, INC_STOP, INC_STEP);
    graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    graph->RunPass<Cleanup>();

    auto graph_unroll = CreateEmptyGraph();
    GRAPH(graph_unroll)
    {
        CONSTANT(0, 10);
        CONSTANT(1, 0);  // a = 0, b = 0
        CONSTANT(2, 1);  // UNROLL_FACTOR - 1 = 1
        // NB: add a new constant if UNROLL_FACTOR is changed and fix INST(20, Opcode::Sub).

        BASIC_BLOCK(2, 3, 5)
        {
            // NB: replace the second input if UNROLL_FACTOR is changed:
            INST(20, Opcode::Sub).s32().Inputs(0, 2);
            INST(3, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(1, 20);  // if (a < 10 -
                                                                                            // (UNROLL_FACTOR - 1))
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(3);
        }
        BASIC_BLOCK(3, 3, 5)
        {
            INST(5, Opcode::Phi).s32().Inputs(1, 21);   // a
            INST(6, Opcode::Phi).s32().Inputs(1, 22);   // b
            INST(7, Opcode::Add).s32().Inputs(5, 2);    // a + 1
            INST(8, Opcode::Add).s32().Inputs(6, 7);    // b + a
            INST(21, Opcode::Add).s32().Inputs(7, 2);   // a + 1
            INST(22, Opcode::Add).s32().Inputs(8, 21);  // b + a
            INST(9, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(21, 20);
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(9);  // if a < 10 - (UNROLL_FACTOR -
                                                                                         // 1)
        }
        BASIC_BLOCK(5, -1)
        {
            INST(11, Opcode::Phi).s32().Inputs(1, 8);
            INST(12, Opcode::Return).s32().Inputs(11);  // return b
        }
    }
    EXPECT_TRUE(GraphComparator().Compare(graph, graph_unroll));
}

TEST_F(LoopUnrollTest, ConstCountableLoopWithIncrementLeftoverIterations)
{
    static constexpr uint32_t INC_STEP = 1;
    static constexpr uint32_t INC_STOP = 11;
    static constexpr uint32_t UNROLL_FACTOR = 3;
    auto graph = BuildLoopWithIncrement(CC_LT, ZERO_START, INC_STOP, INC_STEP);
    graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    graph->RunPass<Cleanup>();

    auto graph_unroll = CreateEmptyGraph();
    GRAPH(graph_unroll)
    {
        CONSTANT(0, 11);
        CONSTANT(1, 0);  // a = 0, b = 0
        CONSTANT(2, 1);
        CONSTANT(32, 2);  // UNROLL_FACTOR - 1 = 2
        // NB: update constant if UNROLL_FACTOR is changed

        BASIC_BLOCK(2, 3, 5)
        {
            INST(20, Opcode::Sub).s32().Inputs(0, 32);
            INST(3, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(1, 20);  // if (a < 11 -
                                                                                            // (UNROLL_FACTOR - 1))
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(3);
        }
        BASIC_BLOCK(3, 3, 5)
        {
            INST(5, Opcode::Phi).s32().Inputs(1, 23);    // a
            INST(6, Opcode::Phi).s32().Inputs(1, 24);    // b
            INST(7, Opcode::Add).s32().Inputs(5, 2);     // a + 1
            INST(8, Opcode::Add).s32().Inputs(6, 7);     // b + a
            INST(21, Opcode::Add).s32().Inputs(7, 2);    // a + 1
            INST(22, Opcode::Add).s32().Inputs(8, 21);   // b + a
            INST(23, Opcode::Add).s32().Inputs(21, 2);   // a + 1
            INST(24, Opcode::Add).s32().Inputs(22, 23);  // b + a
            INST(9, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(23, 20);
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(9);  // if a < 11 - (UNROLL_FACTOR -
                                                                                         // 1)
        }
        BASIC_BLOCK(5, -1)
        {
            INST(25, Opcode::Phi).s32().Inputs(1, 23);   // a
            INST(26, Opcode::Phi).s32().Inputs(1, 24);   // b
            INST(27, Opcode::Add).s32().Inputs(25, 2);   // a + 1
            INST(28, Opcode::Add).s32().Inputs(26, 27);  // b + a
            INST(12, Opcode::Return).s32().Inputs(28);   // return b
        }
    }
    EXPECT_TRUE(GraphComparator().Compare(graph, graph_unroll));
}

// LoopUnroll used to fail by assertion on this test
TEST_F(LoopUnrollTest, ConstCountableLoopAlreadySplitBackEdge)
{
    static constexpr uint32_t INC_STEP = 1;
    static constexpr uint32_t INC_STOP = 11;
    static constexpr uint32_t UNROLL_FACTOR = 3;
    auto graph = BuildLoopWithIncrement(CC_LT, ZERO_START, INC_STOP, INC_STEP);

    auto clone = GraphCloner(graph, graph->GetAllocator(), graph->GetLocalAllocator()).CloneGraph();
    clone->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    clone->RunPass<Cleanup>();

    auto split_after = &INS(8);
    ASSERT(split_after->GetOpcode() == Opcode::Add);
    BB(3).SplitBlockAfterInstruction(split_after, true);
    graph->InvalidateAnalysis<LoopAnalyzer>();
    GraphChecker(graph).Check();
    graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    graph->RunPass<Cleanup>();

    EXPECT_TRUE(GraphComparator().Compare(graph, clone));
}

TEST_F(LoopUnrollTest, SmallCountableLoopWithIncrement)
{
    static constexpr uint32_t INC_STEP = 1;
    static constexpr uint32_t INC_STOP = 4;
    static constexpr uint32_t UNROLL_FACTOR = 2;

    auto graph = BuildLoopWithIncrement(CC_LT, ZERO_START, INC_STOP, INC_STEP);
    graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    graph->RunPass<Cleanup>();
    EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, 6));

    auto graph2 = BuildLoopWithIncrement(CC_LE, ZERO_START, INC_STOP - 1, INC_STEP);
    graph2->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    graph2->RunPass<Cleanup>();
    EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph2, 6));

    auto graph_unroll = CreateEmptyGraph();
    GRAPH(graph_unroll)
    {
        CONSTANT(1, 0);  // a = 0, b = 0
        CONSTANT(2, 1);

        BASIC_BLOCK(2, -1)
        {
            INST(7, Opcode::Add).s32().Inputs(1, 2);     // a + 1
            INST(8, Opcode::Add).s32().Inputs(1, 7);     // b + a
            INST(13, Opcode::Add).s32().Inputs(7, 2);    // a + 1
            INST(14, Opcode::Add).s32().Inputs(8, 13);   // b + a
            INST(15, Opcode::Add).s32().Inputs(13, 2);   // a + 1
            INST(16, Opcode::Add).s32().Inputs(14, 15);  // b + a
            INST(12, Opcode::Return).s32().Inputs(16);   // return b
        }
    }
    EXPECT_TRUE(GraphComparator().Compare(graph, graph_unroll));
    EXPECT_TRUE(GraphComparator().Compare(graph2, graph_unroll));
}

// empty loop: for (i = -1; i >= 0; i += -1)
TEST_F(LoopUnrollTest, EmptyCountableLoopWithNegativeIncrement)
{
    static constexpr uint64_t INC_STEP = -1;
    static constexpr uint64_t INC_START = -1;
    static constexpr uint64_t INC_STOP = 0;
    static constexpr uint32_t UNROLL_FACTOR = 2;

    auto graph = BuildLoopWithIncrement(CC_GE, INC_START, INC_STOP, INC_STEP);
    // unroll with side exits; such loop will be removed after BranchElimination
    EXPECT_TRUE(graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR));
    EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, 0));
}

// loop with two iterations: for (i = 0 + -1; i >= -2; i += -1)
TEST_F(LoopUnrollTest, SmallCountableLoopWithNegativeIncrement)
{
    static constexpr uint64_t INC_STEP = -1;
    static constexpr uint64_t INC_STOP = -2;
    static constexpr uint32_t UNROLL_FACTOR = 2;

    auto graph = BuildLoopWithIncrement(CC_GE, ZERO_START, INC_STOP, INC_STEP);
    EXPECT_TRUE(graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR));
    EXPECT_TRUE(graph->RunPass<Cleanup>());
    EXPECT_TRUE(CheckRetOnVixlSimulator<int32_t>(graph, -3));

    auto graph_unroll = CreateEmptyGraph();
    GRAPH(graph_unroll)
    {
        CONSTANT(1, 0);  // a = 0, b = 0
        CONSTANT(2, -1);

        BASIC_BLOCK(2, -1)
        {
            INST(7, Opcode::Add).s32().Inputs(1, 2);    // a - 1
            INST(8, Opcode::Add).s32().Inputs(1, 7);    // b + a
            INST(13, Opcode::Add).s32().Inputs(7, 2);   // a - 1
            INST(14, Opcode::Add).s32().Inputs(8, 13);  // b + a
            INST(12, Opcode::Return).s32().Inputs(14);  // return b
        }
    }
    EXPECT_TRUE(GraphComparator().Compare(graph, graph_unroll));
}

TEST_F(LoopUnrollTest, EmptyCountableLoop)
{
    static constexpr uint32_t INC_STEP = 1;
    static constexpr uint32_t INC_STOP = 1;
    static constexpr uint32_t UNROLL_FACTOR = 2;

    auto graph = BuildLoopWithIncrement(CC_LT, ZERO_START, INC_STOP, INC_STEP);
    graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    graph->RunPass<Cleanup>();

    auto graph_unroll = CreateEmptyGraph();
    GRAPH(graph_unroll)
    {
        CONSTANT(1, 0);

        BASIC_BLOCK(2, -1)
        {
            INST(12, Opcode::Return).s32().Inputs(1);
        }
    }
    EXPECT_TRUE(GraphComparator().Compare(graph, graph_unroll));
}

TEST_F(LoopUnrollTest, CountableLoopOverflow)
{
    static constexpr uint32_t INC_STEP = 2;
    static constexpr uint32_t INC_STOP = std::numeric_limits<int32_t>::max();
    static constexpr uint32_t UNROLL_FACTOR = 2;

    auto graph = BuildLoopWithIncrement(CC_LT, INC_STOP - 4, INC_STOP, INC_STEP);
    auto clone = GraphCloner(graph, graph->GetAllocator(), graph->GetLocalAllocator()).CloneGraph();
    // small INST_LIMIT allows only unrolling with constant iterations, but such unrolling
    // is impossible due to index overflow
    graph->RunPass<LoopUnroll>(6, UNROLL_FACTOR);

    EXPECT_TRUE(GraphComparator().Compare(graph, clone));
}

TEST_F(LoopUnrollTest, NonConstCountableLoopWithIncrement)
{
    static constexpr uint32_t INC_STEP = 1;
    static constexpr uint32_t INC_STOP = 10;
    for (size_t unroll_factor = 1; unroll_factor <= 10; unroll_factor++) {
        auto graph = BuildLoopWithIncrement(CC_LT, std::nullopt, INC_STOP, INC_STEP);
        graph->RunPass<LoopUnroll>(INST_LIMIT, unroll_factor);
        // 3 + 4 + ... + 9
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, 42, 2));

        graph = BuildLoopWithIncrement(CC_LE, std::nullopt, INC_STOP, INC_STEP);
        graph->RunPass<LoopUnroll>(INST_LIMIT, unroll_factor);
        // 3 + 4 + ... + 10
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, 52, 2));
    }
    static constexpr uint32_t UNROLL_FACTOR = 2;
    auto graph = BuildLoopWithIncrement(CC_LT, std::nullopt, INC_STOP, INC_STEP);
    graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    graph->RunPass<Cleanup>();

    auto graph_unroll = CreateEmptyGraph();
    GRAPH(graph_unroll)
    {
        CONSTANT(0, 10);
        CONSTANT(1, 0);          // b = 0
        CONSTANT(2, 1);          // UNROLL_FACTOR - 1 = 1
        PARAMETER(29, 0).s32();  // a
        // NB: add a new constant if UNROLL_FACTOR is changed and fix INST(20, Opcode::Sub).

        BASIC_BLOCK(2, 3, 5)
        {
            // NB: replace the second input if UNROLL_FACTOR is changed:
            INST(20, Opcode::Sub).s32().Inputs(0, 2);
            INST(3, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(29, 20);  // if (a < 10 -
                                                                                             // (UNROLL_FACTOR - 1))
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(3);
        }
        BASIC_BLOCK(3, 3, 5)
        {
            INST(5, Opcode::Phi).s32().Inputs(29, 21);  // a
            INST(6, Opcode::Phi).s32().Inputs(1, 22);   // b
            INST(7, Opcode::Add).s32().Inputs(5, 2);    // a + 1
            INST(8, Opcode::Add).s32().Inputs(6, 7);    // b + 1
            INST(21, Opcode::Add).s32().Inputs(7, 2);   // a + 1
            INST(22, Opcode::Add).s32().Inputs(8, 21);  // b + 1
            INST(9, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(21, 20);
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(9);  // if a < 10 - (UNROLL_FACTOR -
                                                                                         // 1)
        }
        BASIC_BLOCK(5, 6, 4)
        {
            INST(11, Opcode::Phi).s32().Inputs(1, 8);
            INST(25, Opcode::Phi).s32().Inputs(29, 21);                                      // a
            INST(26, Opcode::Phi).s32().Inputs(1, 22);                                       // b
            INST(27, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(25, 0);  // if (a < 10)
            INST(28, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(27);
        }
        BASIC_BLOCK(6, 4) {}
        BASIC_BLOCK(4, -1)
        {
            INST(31, Opcode::Phi).s32().Inputs(11, 26);
            INST(12, Opcode::Return).s32().Inputs(31);  // return b
        }
    }
    EXPECT_TRUE(GraphComparator().Compare(graph, graph_unroll));
}

TEST_F(LoopUnrollTest, ConstCountableLoopWithDecrement)
{
    static constexpr uint32_t DEC_STEP = 1;
    static constexpr uint32_t DEC_START = 10;
    for (size_t unroll_factor = 1; unroll_factor <= 10; unroll_factor++) {
        auto graph = BuildLoopWithDecrement(CC_GT, DEC_START, ZERO_STOP, DEC_STEP);
        graph->RunPass<LoopUnroll>(INST_LIMIT, unroll_factor);
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, 55));

        graph = BuildLoopWithDecrement(CC_GE, DEC_START, ZERO_STOP, DEC_STEP);
        graph->RunPass<LoopUnroll>(INST_LIMIT, unroll_factor);
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, 55));
    }

    static constexpr uint32_t UNROLL_FACTOR = 2;
    auto graph = BuildLoopWithDecrement(CC_GT, DEC_START, ZERO_STOP, DEC_STEP);
    graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    graph->RunPass<Cleanup>();

    auto graph_unroll = CreateEmptyGraph();
    GRAPH(graph_unroll)
    {
        CONSTANT(0, 10);  // a = 10
        CONSTANT(1, 0);   // b = 0
        CONSTANT(2, 1);   // UNROLL_FACTOR - 1 = 1
        // NB: add a new constant if UNROLL_FACTOR is changed and fix INST(20, Opcode::Add).

        BASIC_BLOCK(2, 3, 5)
        {
            // NB: replace the second input if UNROLL_FACTOR is changed:
            INST(20, Opcode::Add).s32().Inputs(1, 2);
            INST(3, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_GT).Inputs(0, 20);  // if (a > UNROLL_FACTOR -
                                                                                            // 1)
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(3);
        }
        BASIC_BLOCK(3, 3, 5)
        {
            INST(5, Opcode::Phi).s32().Inputs(0, 22);                                        // a
            INST(6, Opcode::Phi).s32().Inputs(1, 21);                                        // b
            INST(7, Opcode::Add).s32().Inputs(6, 5);                                         // b += a
            INST(8, Opcode::Sub).s32().Inputs(5, 2);                                         // a -= 1
            INST(21, Opcode::Add).s32().Inputs(7, 8);                                        // b += a
            INST(22, Opcode::Sub).s32().Inputs(8, 2);                                        // a -= 1
            INST(9, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_GT).Inputs(22, 20);  // if (a > UNROLL_FACTOR -
                                                                                             // 1)
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(9);
        }
        BASIC_BLOCK(5, -1)
        {
            INST(25, Opcode::Phi).s32().Inputs(1, 21);  // b
            INST(12, Opcode::Return).s32().Inputs(25);  // return b
        }
    }
    EXPECT_TRUE(GraphComparator().Compare(graph, graph_unroll));
}

TEST_F(LoopUnrollTest, SmallCountableLoopWithDecrement)
{
    static constexpr uint32_t DEC_STEP = 1;
    static constexpr uint32_t DEC_START = 4;
    static constexpr uint32_t UNROLL_FACTOR = 2;

    auto graph = BuildLoopWithDecrement(CC_GT, DEC_START, ZERO_STOP, DEC_STEP);
    graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    graph->RunPass<Cleanup>();
    EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, 10));

    auto graph_unroll = CreateEmptyGraph();
    GRAPH(graph_unroll)
    {
        CONSTANT(0, 4);  // a = 4
        CONSTANT(1, 0);  // b = 0
        CONSTANT(2, 1);
        // NB: add a new constant if UNROLL_FACTOR is changed and fix INST(20, Opcode::Sub).

        BASIC_BLOCK(2, -1)
        {
            INST(7, Opcode::Add).s32().Inputs(1, 0);     // b + a
            INST(8, Opcode::Sub).s32().Inputs(0, 2);     // a - 1
            INST(13, Opcode::Add).s32().Inputs(7, 8);    // b + a
            INST(14, Opcode::Sub).s32().Inputs(8, 2);    // a - 1
            INST(15, Opcode::Add).s32().Inputs(13, 14);  // b + a
            INST(16, Opcode::Sub).s32().Inputs(14, 2);   // a - 1
            INST(17, Opcode::Add).s32().Inputs(15, 16);  // b + a
            INST(12, Opcode::Return).s32().Inputs(17);   // return b
        }
    }
    EXPECT_TRUE(GraphComparator().Compare(graph, graph_unroll));
}

TEST_F(LoopUnrollTest, NonConstCountableLoopWithDecrement)
{
    static constexpr uint32_t DEC_STEP = 1;
    for (size_t unroll_factor = 1; unroll_factor <= 10; unroll_factor++) {
        auto graph = BuildLoopWithDecrement(CC_GT, std::nullopt, ZERO_STOP, DEC_STEP);
        graph->RunPass<LoopUnroll>(INST_LIMIT, unroll_factor);
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, 55, 10));

        graph = BuildLoopWithDecrement(CC_GE, std::nullopt, ZERO_STOP, DEC_STEP);
        graph->RunPass<LoopUnroll>(INST_LIMIT, unroll_factor);
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, 55, 10));
    }

    static constexpr uint32_t UNROLL_FACTOR = 2;
    auto graph = BuildLoopWithDecrement(CC_GT, std::nullopt, ZERO_STOP, DEC_STEP);
    graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    graph->RunPass<Cleanup>();

    auto graph_unroll = CreateEmptyGraph();
    GRAPH(graph_unroll)
    {
        PARAMETER(0, 0).s32();  // a
        CONSTANT(1, 0);         // b = 0
        CONSTANT(2, 1);         // UNROLL_FACTOR - 1 = 1
        // NB: add a new constant if UNROLL_FACTOR is changed and fix INST(20, Opcode::Add).

        BASIC_BLOCK(2, 3, 5)
        {
            // NB: replace the second input if UNROLL_FACTOR is changed:
            INST(20, Opcode::Add).s32().Inputs(1, 2);
            INST(3, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_GT).Inputs(0, 20);  // if (a > UNROLL_FACTOR -
                                                                                            // 1)
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(3);
        }
        BASIC_BLOCK(3, 3, 5)
        {
            INST(5, Opcode::Phi).s32().Inputs(0, 22);                                        // a
            INST(6, Opcode::Phi).s32().Inputs(1, 21);                                        // b
            INST(7, Opcode::Add).s32().Inputs(6, 5);                                         // b += a
            INST(8, Opcode::Sub).s32().Inputs(5, 2);                                         // a -= 1
            INST(21, Opcode::Add).s32().Inputs(7, 8);                                        // b += a
            INST(22, Opcode::Sub).s32().Inputs(8, 2);                                        // a -= 1
            INST(9, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_GT).Inputs(22, 20);  // if (a > UNROLL_FACTOR -
                                                                                             // 1)
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(9);
        }
        BASIC_BLOCK(5, 6, 4)
        {
            INST(25, Opcode::Phi).s32().Inputs(1, 21);                                       // b
            INST(26, Opcode::Phi).s32().Inputs(0, 22);                                       // a
            INST(27, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_GT).Inputs(26, 1);  // if (a > 0)
            INST(28, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(27);
        }
        BASIC_BLOCK(6, 4)
        {
            INST(29, Opcode::Add).s32().Inputs(25, 26);  // b += a
        }
        BASIC_BLOCK(4, -1)
        {
            INST(31, Opcode::Phi).s32().Inputs(25, 29);
            INST(12, Opcode::Return).s32().Inputs(31);  // return b
        }
    }
    EXPECT_TRUE(GraphComparator().Compare(graph, graph_unroll));
}

TEST_F(LoopUnrollTest, UnsignedCountableLoopWithIncrementConditionOverflow)
{
    static constexpr uint32_t INC_STOP = 3;
    static constexpr uint32_t INC_STEP = 1;
    for (size_t unroll_factor = 4; unroll_factor <= 8; unroll_factor++) {
        auto graph = BuildLoopWithIncrement(CC_LE, std::nullopt, INC_STOP, INC_STEP, DataType::UINT32);
        EXPECT_EQ(5, graph->GetAliveBlocksCount());
        EXPECT_TRUE(graph->RunPass<LoopUnroll>(INST_LIMIT, unroll_factor));
        if (unroll_factor == 4) {
            // unroll without side exits
            EXPECT_EQ(18, graph->GetAliveBlocksCount());
        } else {
            // do not unroll without side exits because new condition in header would overflow
            EXPECT_EQ(6 + unroll_factor, graph->GetAliveBlocksCount());
        }
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, 6, ZERO_START));
    }
}

TEST_F(LoopUnrollTest, UnsignedCountableLoopWithDecrementConditionOverflow)
{
    static constexpr uint32_t DEC_STOP = std::numeric_limits<uint32_t>::max() - 3;
    static constexpr uint32_t DEC_STEP = 2;
    for (size_t unroll_factor = 3; unroll_factor <= 6; unroll_factor++) {
        auto graph = BuildLoopWithDecrement(CC_GT, std::nullopt, DEC_STOP, DEC_STEP, DataType::UINT32);
        EXPECT_EQ(5, graph->GetAliveBlocksCount());
        EXPECT_TRUE(graph->RunPass<LoopUnroll>(INST_LIMIT, unroll_factor));
        // do not unroll without side exits because new condition in header would overflow
        EXPECT_EQ(6 + unroll_factor, graph->GetAliveBlocksCount());
    }
}

TEST_F(LoopUnrollTest, UnsignedCountableLoopWithDecrementSmallValues)
{
    static constexpr uint32_t DEC_START = 10;
    static constexpr uint32_t DEC_STOP = 2;
    static constexpr uint32_t DEC_STEP = 1;
    for (size_t unroll_factor = 1; unroll_factor <= 10; unroll_factor++) {
        bool unrolled = unroll_factor > 1;
        auto graph = BuildLoopWithDecrement(CC_GT, std::nullopt, std::nullopt, DEC_STEP, DataType::UINT32);
        EXPECT_EQ(unrolled, graph->RunPass<LoopUnroll>(INST_LIMIT, unroll_factor));
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, 52, DEC_START, DEC_STOP));

        graph = BuildLoopWithDecrement(CC_GE, std::nullopt, std::nullopt, DEC_STEP, DataType::UINT32);
        EXPECT_EQ(unrolled, graph->RunPass<LoopUnroll>(INST_LIMIT, unroll_factor));
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, 54, DEC_START, DEC_STOP));
    }
}

TEST_F(LoopUnrollTest, UnsignedCountableLoopWithDecrementLargeValues)
{
    static constexpr uint32_t DEC_STEP = 1;
    static constexpr uint32_t DEC_START = std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t DEC_STOP = DEC_START - 3;
    uint32_t result_gt = 0;
    for (auto i = DEC_START; i > DEC_STOP; i--) {
        result_gt += i;
    }
    uint32_t result_ge = result_gt + DEC_STOP;

    for (size_t unroll_factor = 1; unroll_factor <= 10; unroll_factor++) {
        bool unrolled = unroll_factor > 1;
        auto graph = BuildLoopWithDecrement(CC_GT, std::nullopt, std::nullopt, DEC_STEP, DataType::UINT32);
        EXPECT_EQ(unrolled, graph->RunPass<LoopUnroll>(INST_LIMIT, unroll_factor));
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, result_gt, DEC_START, DEC_STOP));

        graph = BuildLoopWithDecrement(CC_GE, std::nullopt, std::nullopt, DEC_STEP, DataType::UINT32);
        EXPECT_EQ(unrolled, graph->RunPass<LoopUnroll>(INST_LIMIT, unroll_factor));
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, result_ge, DEC_START, DEC_STOP));
    }
}

TEST_F(LoopUnrollTest, InversedCompares)
{
    // Case 1: if (a < 10 is false) goto exit
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        PARAMETER(13, 0).s32();  // a; pass as a parameter to check for unknown number of iterations
        CONSTANT(0, 10);
        CONSTANT(1, 0);  // b = 0
        CONSTANT(2, 1);
        BASIC_BLOCK(2, 4, 3)
        {
            INST(3, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(13, 0);
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_EQ).Imm(0).Inputs(3);  // if a < 10 goto loop
        }
        BASIC_BLOCK(3, 4, 3)
        {
            INST(5, Opcode::Phi).s32().Inputs(13, 7);  // a
            INST(6, Opcode::Phi).s32().Inputs(1, 8);   // b
            INST(7, Opcode::Add).s32().Inputs(5, 2);   // a += 1
            INST(8, Opcode::Add).s32().Inputs(6, 7);   // b += a
            INST(9, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(7, 0);
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_EQ).Imm(0).Inputs(9);  // if a < 10 goto loop
        }
        BASIC_BLOCK(4, -1)
        {
            INST(11, Opcode::Phi).s32().Inputs(1, 6);
            INST(12, Opcode::Return).s32().Inputs(11);  // return b
        }
    }

    static constexpr uint32_t UNROLL_FACTOR = 2;
    graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    graph->RunPass<Cleanup>();

    auto graph_unroll = CreateEmptyGraph();
    GRAPH(graph_unroll)
    {
        PARAMETER(13, 0).s32();  // a
        CONSTANT(0, 10);
        CONSTANT(1, 0);  // b = 0
        CONSTANT(2, 1);  // UNROLL_FACTOR - 1 = 1
        // NB: add a new constant if UNROLL_FACTOR is changed and fix INST(20, Opcode::Sub).

        BASIC_BLOCK(2, 3, 5)
        {
            // NB: replace the second input if UNROLL_FACTOR is changed:
            INST(20, Opcode::Sub).s32().Inputs(0, 2);
            INST(3, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(13, 20);
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(3);  // if (a <= 10 - UNROLL_FACTOR)
        }
        BASIC_BLOCK(3, 3, 5)
        {
            INST(5, Opcode::Phi).s32().Inputs(13, 21);  // a
            INST(6, Opcode::Phi).s32().Inputs(1, 22);   // b
            INST(7, Opcode::Add).s32().Inputs(5, 2);    // a + 1
            INST(8, Opcode::Add).s32().Inputs(6, 7);    // b + 1
            INST(21, Opcode::Add).s32().Inputs(7, 2);   // a + 1
            INST(22, Opcode::Add).s32().Inputs(8, 21);  // b + 1
            INST(9, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(21, 20);
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(9);  // if (a <= 10 - UNROLL_FACTOR)
        }
        BASIC_BLOCK(5, 4, 6)
        {
            INST(11, Opcode::Phi).s32().Inputs(1, 8);
            INST(25, Opcode::Phi).s32().Inputs(13, 21);                                      // a
            INST(26, Opcode::Phi).s32().Inputs(1, 22);                                       // b
            INST(27, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(25, 0);  // if (a < 10)
            INST(28, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_EQ).Imm(0).Inputs(27);
        }
        BASIC_BLOCK(6, 4) {}
        BASIC_BLOCK(4, -1)
        {
            INST(31, Opcode::Phi).s32().Inputs(11, 26);
            INST(12, Opcode::Return).s32().Inputs(31);  // return b
        }
    }
    EXPECT_TRUE(GraphComparator().Compare(graph, graph_unroll));
    EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, 45, 0));

    // Case 2: if (a >= 10 is false) goto loop
    auto graph2 = CreateEmptyGraph();
    GRAPH(graph2)
    {
        PARAMETER(13, 0).s32();  // a
        CONSTANT(0, 10);
        CONSTANT(1, 0);  // b = 0
        CONSTANT(2, 1);
        BASIC_BLOCK(2, 3, 4)
        {
            INST(3, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_GE).Inputs(13, 0);
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_EQ).Imm(0).Inputs(3);  // if a < 10 goto loop
        }
        BASIC_BLOCK(3, 3, 4)
        {
            INST(5, Opcode::Phi).s32().Inputs(13, 7);  // a
            INST(6, Opcode::Phi).s32().Inputs(1, 8);   // b
            INST(7, Opcode::Add).s32().Inputs(5, 2);   // a += 1
            INST(8, Opcode::Add).s32().Inputs(6, 7);   // b += a
            INST(9, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_GE).Inputs(7, 0);
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_EQ).Imm(0).Inputs(9);  // if a < 10 goto loop
        }
        BASIC_BLOCK(4, -1)
        {
            INST(11, Opcode::Phi).s32().Inputs(1, 6);
            INST(12, Opcode::Return).s32().Inputs(11);  // return b
        }
    }

    graph2->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    graph2->RunPass<Cleanup>();

    auto graph2_unroll = CreateEmptyGraph();
    GRAPH(graph2_unroll)
    {
        PARAMETER(13, 0).s32();  // a
        CONSTANT(0, 10);
        CONSTANT(1, 0);  // b = 0
        CONSTANT(2, 1);  // UNROLL_FACTOR - 1 = 1
        // NB: add a new constant if UNROLL_FACTOR is changed and fix INST(20, Opcode::Sub).

        BASIC_BLOCK(2, 3, 5)
        {
            // NB: replace the second input if UNROLL_FACTOR is changed:
            INST(20, Opcode::Sub).s32().Inputs(0, 2);
            INST(3, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(13, 20);
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(3);  // if (a <= 10 - UNROLL_FACTOR)
        }
        BASIC_BLOCK(3, 3, 5)
        {
            INST(5, Opcode::Phi).s32().Inputs(13, 21);  // a
            INST(6, Opcode::Phi).s32().Inputs(1, 22);   // b
            INST(7, Opcode::Add).s32().Inputs(5, 2);    // a + 1
            INST(8, Opcode::Add).s32().Inputs(6, 7);    // b + 1
            INST(21, Opcode::Add).s32().Inputs(7, 2);   // a + 1
            INST(22, Opcode::Add).s32().Inputs(8, 21);  // b + 1
            INST(9, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(21, 20);
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(9);  // if (a <= 10 - UNROLL_FACTOR)
        }
        BASIC_BLOCK(5, 6, 4)
        {
            INST(11, Opcode::Phi).s32().Inputs(1, 8);
            INST(25, Opcode::Phi).s32().Inputs(13, 21);                                      // a
            INST(26, Opcode::Phi).s32().Inputs(1, 22);                                       // b
            INST(27, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_GE).Inputs(25, 0);  // if (a < 10)
            INST(28, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_EQ).Imm(0).Inputs(27);
        }
        BASIC_BLOCK(6, 4) {}
        BASIC_BLOCK(4, -1)
        {
            INST(31, Opcode::Phi).s32().Inputs(11, 26);
            INST(12, Opcode::Return).s32().Inputs(31);  // return b
        }
    }
    EXPECT_TRUE(GraphComparator().Compare(graph2, graph2_unroll));
    EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph2, 45, 0));

    // Case 3 - if (10 != a) goto loop
    auto graph3 = CreateEmptyGraph();
    GRAPH(graph3)
    {
        CONSTANT(0, 10);
        CONSTANT(1, 0);  // a = 0, b = 0
        CONSTANT(2, 1);
        BASIC_BLOCK(2, 4, 3)
        {
            INST(3, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_NE).Inputs(0, 1);
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_EQ).Imm(0).Inputs(3);  // if 10 != a goto loop
        }
        BASIC_BLOCK(3, 4, 3)
        {
            INST(5, Opcode::Phi).s32().Inputs(1, 7);  // a
            INST(6, Opcode::Phi).s32().Inputs(1, 8);  // b
            INST(7, Opcode::Add).s32().Inputs(5, 2);  // a += 1
            INST(8, Opcode::Add).s32().Inputs(6, 7);  // b += a
            INST(9, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_NE).Inputs(0, 7);
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_EQ).Imm(0).Inputs(9);  // if 10 != a goto loop
        }
        BASIC_BLOCK(4, -1)
        {
            INST(11, Opcode::Phi).s32().Inputs(1, 6);
            INST(12, Opcode::Return).s32().Inputs(11);  // return b
        }
    }
    graph3->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph3, 45));

    // Case 4 (decrement): if (0 == a) goto out_loop
    auto graph4 = CreateEmptyGraph();
    GRAPH(graph4)
    {
        CONSTANT(0, 9);  // a = 9
        CONSTANT(1, 0);  // b = 0
        CONSTANT(2, 1);

        BASIC_BLOCK(2, 4, 3)
        {
            INST(3, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_EQ).Inputs(1, 0);  // if 0 == a goto out_loop
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(3);
        }
        BASIC_BLOCK(3, 4, 3)
        {
            INST(5, Opcode::Phi).s32().Inputs(0, 8);                                       // a
            INST(6, Opcode::Phi).s32().Inputs(1, 7);                                       // b
            INST(7, Opcode::Add).s32().Inputs(6, 5);                                       // b += a
            INST(8, Opcode::Sub).s32().Inputs(5, 2);                                       // a -= 1
            INST(9, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_EQ).Inputs(1, 8);  // if 0 == a goto out_loop
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(9);
        }
        BASIC_BLOCK(4, -1)
        {
            INST(11, Opcode::Phi).s32().Inputs(1, 7);
            INST(12, Opcode::Return).s32().Inputs(11);  // return b
        }
    }
    graph4->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph4, 45));
}

TEST_F(LoopUnrollTest, LoopWithDifferentConstants)
{
    static constexpr uint32_t UNROLL_FACTOR = 2;

    // Chech increment
    static constexpr uint32_t INC_STOP = 100;
    for (size_t inc_step = 1; inc_step <= 10; inc_step++) {
        // CC_LT
        size_t result = 0;
        for (size_t i = 0; i < INC_STOP; i += inc_step) {
            result += i;
        }
        auto graph = BuildLoopWithIncrement(CC_LT, ZERO_START, INC_STOP, inc_step);
        EXPECT_TRUE(graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR));
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, result));

        // CC_LE
        result = 0;
        for (size_t i = 0; i <= INC_STOP; i += inc_step) {
            result += i;
        }
        graph = BuildLoopWithIncrement(CC_LE, ZERO_START, INC_STOP, inc_step);
        EXPECT_TRUE(graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR));
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, result));

        // CC_NE
        if (INC_STOP % inc_step != 0) {
            // Otherwise test loop with CC_NE will be infinite
            continue;
        }
        result = 0;
        for (size_t i = 0; i != INC_STOP; i += inc_step) {
            result += i;
        }
        graph = BuildLoopWithIncrement(CC_NE, ZERO_START, INC_STOP, inc_step);
        EXPECT_TRUE(graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR));
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, result));
    }

    // Chech decrement
    static constexpr uint32_t DEC_START = 100;
    for (size_t dec_step = 1; dec_step <= 10; dec_step++) {
        // CC_GT
        int result = 0;
        for (int i = DEC_START; i > 0; i -= dec_step) {
            result += i;
        }
        auto graph = BuildLoopWithDecrement(CC_GT, DEC_START, ZERO_STOP, dec_step);
        EXPECT_TRUE(graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR));
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, result));

        // CC_GE
        result = 0;
        for (int i = DEC_START; i >= 0; i -= dec_step) {
            result += i;
        }
        graph = BuildLoopWithDecrement(CC_GE, DEC_START, ZERO_STOP, dec_step);
        EXPECT_TRUE(graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR));
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, result));

        // CC_NE
        if (INC_STOP % dec_step != 0) {
            // Otherwise test loop with CC_NE will be infinite
            continue;
        }
        result = 0;
        for (int i = DEC_START; i != 0; i -= dec_step) {
            result += i;
        }
        graph = BuildLoopWithDecrement(CC_NE, DEC_START, ZERO_STOP, dec_step);
        EXPECT_TRUE(graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR));
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, result));
    }
}

TEST_F(LoopUnrollTest, PredsInversedOrder)
{
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        PARAMETER(0, 0).s64();  // a
        PARAMETER(1, 1).s64();  // b
        CONSTANT(2, 1);
        CONSTANT(3, 2);
        BASIC_BLOCK(2, 3, 4)
        {
            INST(6, Opcode::Phi).s64().Inputs(1, 12);                             // b
            INST(7, Opcode::Mod).s64().Inputs(6, 3);                              // b % 2
            INST(8, Opcode::If).SrcType(DataType::INT64).CC(CC_EQ).Inputs(7, 2);  // if b % 2 == 1
        }
        BASIC_BLOCK(3, 4)
        {
            INST(10, Opcode::Mul).s64().Inputs(6, 6);  // b = b * b
        }
        BASIC_BLOCK(4, 2, 5)
        {
            INST(12, Opcode::Phi).s64().Inputs({{3, 10}, {2, 6}});
            INST(13, Opcode::Compare).CC(CC_LT).b().Inputs(12, 0);  // if b < a
            INST(14, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(13);
        }
        BASIC_BLOCK(5, -1)
        {
            INST(15, Opcode::Return).s64().Inputs(12);  // return b
        }
    }
    // Swap BB4 preds
    std::swap(BB(4).GetPredsBlocks()[0], BB(4).GetPredsBlocks()[1]);
    INS(12).SetInput(0, &INS(6));
    INS(12).SetInput(1, &INS(10));
    graph->RunPass<LoopUnroll>(INST_LIMIT, 2);
    graph->RunPass<Cleanup>();

    auto expected_graph = CreateEmptyGraph();
    GRAPH(expected_graph)
    {
        PARAMETER(0, 0).s64();  // a
        PARAMETER(1, 1).s64();  // b
        CONSTANT(2, 1);
        CONSTANT(3, 2);
        BASIC_BLOCK(6, 2) {}
        BASIC_BLOCK(2, 3, 4)
        {
            INST(6, Opcode::Phi).s64().Inputs(1, 19);                             // b
            INST(7, Opcode::Mod).s64().Inputs(6, 3);                              // b % 2
            INST(8, Opcode::If).SrcType(DataType::INT64).CC(CC_EQ).Inputs(7, 2);  // if b % 2 == 1
        }
        BASIC_BLOCK(3, 4)
        {
            INST(10, Opcode::Mul).s64().Inputs(6, 6);  // b = b * b
        }
        BASIC_BLOCK(4, 9, 8)
        {
            INST(12, Opcode::Phi).s64().Inputs({{3, 10}, {2, 6}});
            INST(13, Opcode::Compare).CC(CC_LT).b().Inputs(12, 0);  // if b < a
            INST(14, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(13);
        }
        BASIC_BLOCK(9, 10, 11)
        {
            INST(16, Opcode::Mod).s64().Inputs(12, 3);                              // b % 2
            INST(17, Opcode::If).SrcType(DataType::INT64).CC(CC_EQ).Inputs(16, 2);  // if b % 2 == 1
        }
        BASIC_BLOCK(10, 11)
        {
            INST(18, Opcode::Mul).s64().Inputs(12, 12);  // b = b * b
        }
        BASIC_BLOCK(11, 2, 8)
        {
            INST(19, Opcode::Phi).s64().Inputs({{9, 12}, {10, 18}});
            INST(20, Opcode::Compare).CC(CC_LT).b().Inputs(19, 0);  // if b < a
            INST(21, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(20);
        }
        BASIC_BLOCK(8, -1)
        {
            INST(22, Opcode::Phi).s64().Inputs({{4, 12}, {11, 19}});
            INST(15, Opcode::Return).s64().Inputs(22);  // return b
        }
    }
    EXPECT_TRUE(GraphComparator().Compare(graph, expected_graph));
}

// TODO (a.popov) Fix after supporting infinite loops unrolling
TEST_F(LoopUnrollTest, InfiniteLoop)
{
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        PARAMETER(0, 0).s32();
        CONSTANT(1, 1);

        BASIC_BLOCK(2, 2)
        {
            INST(2, Opcode::Phi).s32().Inputs(0, 3);
            INST(3, Opcode::Add).s32().Inputs(2, 1);
        }
    }
    EXPECT_FALSE(graph->RunPass<LoopUnroll>(1000, 2));
}

TEST_F(LoopUnrollTest, PhiDominatesItsPhiInput)
{
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 0);
        CONSTANT(1, 1);
        CONSTANT(2, 100);

        BASIC_BLOCK(2, 2, 3)
        {
            INST(5, Opcode::Phi).s32().Inputs(0, 7);
            INST(6, Opcode::Phi).s32().Inputs(1, 5);
            INST(7, Opcode::Add).s32().Inputs(5, 6);  // Fibonacci Sequence
            INST(8, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(7, 2);
            INST(9, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(8);
        }
        BASIC_BLOCK(3, -1)
        {
            INST(10, Opcode::Return).s32().Inputs(7);
        }
    }

    static constexpr uint64_t PROGRAM_RESULT = 144;
    for (auto unroll_factor = 2; unroll_factor < 10; ++unroll_factor) {
        auto clone = GraphCloner(graph, graph->GetAllocator(), graph->GetLocalAllocator()).CloneGraph();
        clone->RunPass<LoopUnroll>(INST_LIMIT, unroll_factor);
        EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(clone, PROGRAM_RESULT));
    }
    EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, PROGRAM_RESULT));
}

TEST_F(LoopUnrollTest, BackEdgeWithoutCompare)
{
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 10);
        CONSTANT(1, 0);  // a = 0, b = 0
        CONSTANT(2, 1);

        BASIC_BLOCK(2, 3, 6)
        {
            INST(3, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(1, 0);
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(3);  // if a < 10
        }
        BASIC_BLOCK(3, 4, 5)
        {
            INST(5, Opcode::Phi).s32().Inputs(1, 7);   // a
            INST(6, Opcode::Phi).s32().Inputs(1, 12);  // b
            INST(7, Opcode::Add).s32().Inputs(5, 2);   // a++
            INST(8, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(7, 0);
            INST(9, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(8);  // if a < 10
        }
        BASIC_BLOCK(4, 5)
        {
            INST(10, Opcode::Add).s32().Inputs(6, 2);  // b++
        }
        BASIC_BLOCK(5, 3, 6)
        {
            INST(11, Opcode::Phi).s32().Inputs(6, 10);                                   // b
            INST(12, Opcode::Add).s32().Inputs(11, 7);                                   // b += a
            INST(13, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(8);  // if a < 10
        }
        BASIC_BLOCK(6, -1)
        {
            INST(14, Opcode::Phi).s32().Inputs(1, 12);
            INST(15, Opcode::Return).s32().Inputs(14);  // return b
        }
    }
    auto unrolled_graph = GraphCloner(graph, graph->GetAllocator(), graph->GetLocalAllocator()).CloneGraph();
    EXPECT_TRUE(unrolled_graph->RunPass<LoopUnroll>(INST_LIMIT, 2));

    static constexpr uint64_t PROGRAM_RESULT = 64;
    EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(graph, PROGRAM_RESULT));
    EXPECT_TRUE(CheckRetOnVixlSimulator<uint64_t>(unrolled_graph, PROGRAM_RESULT));
}

TEST_F(LoopUnrollTest, UnrollWithCalls)
{
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        PARAMETER(0, 0).u64();  // a
        PARAMETER(1, 1).u64();  // b

        BASIC_BLOCK(2, 2, 3)
        {
            INST(4, Opcode::Phi).u64().Inputs(0, 6);
            INST(20, Opcode::SaveState).NoVregs();
            INST(5, Opcode::CallStatic).u64().InputsAutoType(20);
            INST(6, Opcode::Add).u64().Inputs(4, 5);              // a += call()
            INST(7, Opcode::Compare).CC(CC_LT).b().Inputs(6, 1);  // while a < b
            INST(8, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(7);
        }

        BASIC_BLOCK(3, -1)
        {
            INST(11, Opcode::Return).u64().Inputs(6);  // return a
        }
    }
    static constexpr auto UNROLL_FACTOR = 5U;
    auto default_is_unroll_with_calls = OPTIONS.IsCompilerUnrollLoopWithCalls();

    // Enable loop unroll with calls
    OPTIONS.SetCompilerUnrollLoopWithCalls(true);
    auto clone = GraphCloner(graph, graph->GetAllocator(), graph->GetLocalAllocator()).CloneGraph();
    EXPECT_TRUE(clone->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR));
    CountOpcodes(clone->GetBlocksRPO());
    EXPECT_EQ(GetOpcodeCount(Opcode::CallStatic), UNROLL_FACTOR);

    // Disable loop unroll with calls
    OPTIONS.SetCompilerUnrollLoopWithCalls(false);
    EXPECT_FALSE(graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR));
    CountOpcodes(graph->GetBlocksRPO());
    EXPECT_EQ(GetOpcodeCount(Opcode::CallStatic), 1U);

    // Return default option
    OPTIONS.SetCompilerUnrollLoopWithCalls(default_is_unroll_with_calls);
}

TEST_F(LoopUnrollTest, AddOverflowUnroll)
{
    // Alwayes make unroll with side exits for AddOverflowCheck instructions
    // Case 1: if (a < 10 is false) goto exit
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 10);
        CONSTANT(1, 0);  // a = 0, b = 0
        CONSTANT(2, 1);
        BASIC_BLOCK(2, 4, 3)
        {
            INST(3, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(1, 0);
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_EQ).Imm(0).Inputs(3);  // if a < 10 goto loop
        }
        BASIC_BLOCK(3, 4, 3)
        {
            INST(5, Opcode::Phi).s32().Inputs(1, 7);  // a
            INST(6, Opcode::Phi).s32().Inputs(1, 8);  // b
            INST(20, Opcode::SaveState).NoVregs();
            INST(7, Opcode::AddOverflowCheck).s32().Inputs(5, 2, 20);  // a += 1
            INST(21, Opcode::SaveState).NoVregs();
            INST(8, Opcode::AddOverflowCheck).s32().Inputs(6, 7, 21);  // b += a
            INST(9, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(7, 0);
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_EQ).Imm(0).Inputs(9);  // if a < 10 goto loop
        }
        BASIC_BLOCK(4, -1)
        {
            INST(11, Opcode::Phi).s32().Inputs(1, 6);
            INST(12, Opcode::Return).s32().Inputs(11);  // return b
        }
    }

    static constexpr uint32_t UNROLL_FACTOR = 2;
    graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR);
    graph->RunPass<Cleanup>();

    auto graph_unroll = CreateEmptyGraph();
    GRAPH(graph_unroll)
    {
        CONSTANT(0, 10);
        CONSTANT(1, 0);  // a = 0, b = 0
        CONSTANT(2, 1);
        BASIC_BLOCK(2, 4, 3)
        {
            INST(3, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(1, 0);
            INST(4, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_EQ).Imm(0).Inputs(3);  // if a < 10 goto loop
        }
        BASIC_BLOCK(3, 4, 5)
        {
            INST(5, Opcode::Phi).s32().Inputs(1, 17);  // a
            INST(6, Opcode::Phi).s32().Inputs(1, 18);  // b
            INST(20, Opcode::SaveState).NoVregs();
            INST(7, Opcode::AddOverflowCheck).s32().Inputs(5, 2, 20);  // a += 1
            INST(21, Opcode::SaveState).NoVregs();
            INST(8, Opcode::AddOverflowCheck).s32().Inputs(6, 7, 21);  // b += a
            INST(9, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(7, 0);
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_EQ).Imm(0).Inputs(9);  // if a < 10 goto loop
        }
        BASIC_BLOCK(5, 4, 3)
        {
            INST(30, Opcode::SaveState).NoVregs();
            INST(17, Opcode::AddOverflowCheck).s32().Inputs(7, 2, 30);  // a += 1
            INST(31, Opcode::SaveState).NoVregs();
            INST(18, Opcode::AddOverflowCheck).s32().Inputs(8, 17, 31);  // b += a
            INST(19, Opcode::Compare).b().SrcType(DataType::INT32).CC(CC_LT).Inputs(17, 0);
            INST(15, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_EQ).Imm(0).Inputs(19);  // if a < 10 goto loop
        }
        BASIC_BLOCK(4, -1)
        {
            INST(11, Opcode::Phi).s32().Inputs(1, 6, 8);
            INST(12, Opcode::Return).s32().Inputs(11);  // return b
        }
    }
    EXPECT_TRUE(GraphComparator().Compare(graph, graph_unroll));
}

TEST_F(LoopUnrollTest, UnrollNeedSaveStateBridge)
{
    auto graph = CreateEmptyGraph();
    GRAPH(graph)
    {
        PARAMETER(0, 0).u64();  // a
        PARAMETER(1, 1).u64();  // b
        PARAMETER(2, 2).ref();  // arr
        CONSTANT(3, 1);

        BASIC_BLOCK(2, 2, 3)
        {
            INST(4, Opcode::Phi).u64().Inputs(0, 6);
            INST(20, Opcode::SaveState).Inputs(2).SrcVregs({0});
            INST(9, Opcode::NullCheck).ref().Inputs(2, 20);
            INST(5, Opcode::StoreArray).u64().Inputs(9, 4, 4);    // arr[a] = a
            INST(6, Opcode::Add).u64().Inputs(4, 3);              // a++
            INST(7, Opcode::Compare).CC(CC_LT).b().Inputs(6, 1);  // while a < b
            INST(8, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(7);
        }

        BASIC_BLOCK(3, -1)
        {
            INST(12, Opcode::SaveState).Inputs(2).SrcVregs({0});
            INST(13, Opcode::CallStatic).v0id().InputsAutoType(12);
            INST(10, Opcode::LoadArray).u64().Inputs(9, 1);
            INST(11, Opcode::Return).u64().Inputs(10);  // return arr[b]
        }
    }

    static constexpr auto UNROLL_FACTOR = 2U;
    EXPECT_TRUE(graph->RunPass<LoopUnroll>(INST_LIMIT, UNROLL_FACTOR));
    graph->RunPass<Cleanup>();

    auto graph_unroll = CreateEmptyGraph();
    GRAPH(graph_unroll)
    {
        PARAMETER(0, 0).u64();  // a
        PARAMETER(1, 1).u64();  // b
        PARAMETER(2, 2).ref();  // arr
        CONSTANT(3, 1);

        BASIC_BLOCK(2, 4, 3)
        {
            INST(4, Opcode::Phi).u64().Inputs(0, 26);
            INST(20, Opcode::SaveState).Inputs(2).SrcVregs({0});
            INST(9, Opcode::NullCheck).ref().Inputs(2, 20);
            INST(5, Opcode::StoreArray).u64().Inputs(9, 4, 4);    // arr[a] = a
            INST(6, Opcode::Add).u64().Inputs(4, 3);              // a++
            INST(7, Opcode::Compare).CC(CC_LT).b().Inputs(6, 1);  // while a < b
            INST(8, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(7);
        }

        BASIC_BLOCK(4, 2, 3)
        {
            INST(23, Opcode::SaveState).Inputs(2).SrcVregs({0});
            INST(24, Opcode::NullCheck).ref().Inputs(2, 23);
            INST(25, Opcode::StoreArray).u64().Inputs(24, 6, 6);    // arr[a] = a
            INST(26, Opcode::Add).u64().Inputs(6, 3);               // a++
            INST(27, Opcode::Compare).CC(CC_LT).b().Inputs(26, 1);  // while a < b
            INST(28, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(27);
        }

        BASIC_BLOCK(3, -1)
        {
            INST(21, Opcode::Phi).ref().Inputs(9, 24);
            // Phi with check inputs should be added to SaveState
            INST(12, Opcode::SaveState).Inputs(2, 21).SrcVregs({0, VirtualRegister::BRIDGE});
            INST(13, Opcode::CallStatic).v0id().InputsAutoType(12);
            INST(10, Opcode::LoadArray).u64().Inputs(21, 1);
            INST(11, Opcode::Return).u64().Inputs(10);  // return arr[b]
        }
    }
    EXPECT_TRUE(GraphComparator().Compare(graph, graph_unroll));
}
// NOLINTEND(readability-magic-numbers)

}  // namespace panda::compiler
