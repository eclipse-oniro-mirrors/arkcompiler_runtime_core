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

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "unit_test.h"
#include "optimizer/analysis/dominators_tree.h"
#include "optimizer/analysis/liveness_analyzer.h"

namespace panda::compiler {
using LiveRanges = ArenaVector<LiveRange>;

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LIVE_RANGES_VECTOR(...) LiveRanges({__VA_ARGS__}, GetAllocator()->Adapter())

class LivenessAnalyzerTest : public GraphTest {
public:
    void CheckSubsequence(const ArenaVector<BasicBlock *> &blocks, const ArenaVector<BasicBlock *> &&subsequence)
    {
        auto subseq_iter = subsequence.begin();
        for (auto block : blocks) {
            if (block == *subseq_iter) {
                if (++subseq_iter == subsequence.end()) {
                    break;
                }
            }
        }
        EXPECT_TRUE(subseq_iter == subsequence.end());
    }
};

// NOLINTBEGIN(readability-magic-numbers)
/*
 * Test Graph:
 *                                    [0]
 *                                     |
 *                                     v
 *                                 /--[2]--\
 *                                /         \
 *                               /           \
 *                              v             V
 *                /----------->[4]---------->[3]<---------[15]-------\
 *                |             |             |                      |
 *                |             v             V                      |
 *               [12]          [5]           [6]<-----------\        |
 *                |             |             |             |        |
 *                |             v             v             |        |
 *                \------------[11]          [7]          [10]       |
 *                              |             |             ^        |
 *                              v             v             |        |
 *                             [13]          [8]---------->[9]       |
 *                              |             |                      |
 *                              v             v                      |
 *                             [1]          [14]---------------------/
 *
 * Linear order:
 * [0, 2, 4, 5, 11, 12, 13, 1, 3, 6, 7, 8, 9, 10, 14, 15, 1]
 */
TEST_F(LivenessAnalyzerTest, LinearizeGraph)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0U, 0U);
        CONSTANT(1U, 1U);
        BASIC_BLOCK(2U, 4U, 3U)
        {
            INST(2U, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0U, 1U);
            INST(3U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(2U);
        }
        BASIC_BLOCK(3U, 6U) {}
        BASIC_BLOCK(4U, 5U, 3U)
        {
            INST(5U, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0U, 1U);
            INST(6U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(5U);
        }
        BASIC_BLOCK(5U, 11U) {}
        BASIC_BLOCK(6U, 7U) {}
        BASIC_BLOCK(7U, 8U) {}
        BASIC_BLOCK(8U, 14U, 9U)
        {
            INST(10U, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0U, 1U);
            INST(11U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(10U);
        }
        BASIC_BLOCK(9U, 10U) {}
        BASIC_BLOCK(10U, 6U) {}
        BASIC_BLOCK(11U, 13U, 12U)
        {
            INST(14U, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0U, 1U);
            INST(15U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(14U);
        }
        BASIC_BLOCK(12U, 4U) {}
        BASIC_BLOCK(13U, -1L)
        {
            INST(17U, Opcode::ReturnVoid);
        }
        BASIC_BLOCK(14U, 15U) {}
        BASIC_BLOCK(15U, 3U) {}
    }
    EXPECT_TRUE(GetGraph()->RunPass<LivenessAnalyzer>());

    const auto &blocks = GetGraph()->GetAnalysis<LivenessAnalyzer>().GetLinearizedBlocks();
    CheckSubsequence(blocks,
                     GetBlocksById(GetGraph(), {0U, 2U, 4U, 5U, 11U, 12U, 13U, 1U, 3U, 6U, 7U, 8U, 9U, 10U, 14U, 15U}));
}

TEST_F(LivenessAnalyzerTest, LinearizeGraph2)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0U, 0U);
        CONSTANT(1U, 1U);

        BASIC_BLOCK(100U, 6U) {}
        BASIC_BLOCK(6U, 2U, 7U)
        {
            INST(2U, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0U, 1U);
            INST(3U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(2U);
        }

        BASIC_BLOCK(2U, 5U) {}
        BASIC_BLOCK(5U, 4U, 3U)
        {
            INST(4U, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0U, 1U);
            INST(5U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(4U);
        }

        BASIC_BLOCK(4U, 21U) {}
        BASIC_BLOCK(21U, 17U, 14U)
        {
            INST(6U, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0U, 1U);
            INST(7U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(6U);
        }

        BASIC_BLOCK(14U, 5U) {}
        BASIC_BLOCK(3U, 6U) {}
        BASIC_BLOCK(7U, -1L)
        {
            INST(14U, Opcode::ReturnVoid);
        }

        BASIC_BLOCK(17U, 18U, 19U)
        {
            INST(8U, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0U, 1U);
            INST(9U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(8U);
        }

        BASIC_BLOCK(18U, 24U, 31U)
        {
            INST(10U, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0U, 1U);
            INST(11U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(10U);
        }

        BASIC_BLOCK(19U, 36U, 43U)
        {
            INST(12U, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0U, 1U);
            INST(13U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(12U);
        }

        BASIC_BLOCK(24U, 20U) {}
        BASIC_BLOCK(36U, 20U) {}
        BASIC_BLOCK(20U, 21U) {}
        BASIC_BLOCK(31U, -1L)
        {
            INST(15U, Opcode::ReturnVoid);
        }
        BASIC_BLOCK(43U, -1L)
        {
            INST(16U, Opcode::ReturnVoid);
        }
    }
    EXPECT_TRUE(GetGraph()->RunPass<LivenessAnalyzer>());

    const auto &blocks = GetGraph()->GetAnalysis<LivenessAnalyzer>().GetLinearizedBlocks();
    CheckSubsequence(blocks,
                     GetBlocksById(GetGraph(), {0, 100, 6, 2, 5, 4, 21, 17, 18, 24, 19, 36, 20, 14, 3, 43, 31, 7, 1}));
}

/*
 *          [0]
 *           |
 *           v
 *          [2]
 *         /   \
 *        v     v
 *      [3]<----[4]
 *       |
 *       v
 *      [1]
 */
TEST_F(LivenessAnalyzerTest, LinearizeGraphWithoutLoops)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(2U, Opcode::Compare).b().Inputs(0U, 1U);
            INST(3U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(2U);
        }
        BASIC_BLOCK(4U, 3U) {}
        BASIC_BLOCK(3U, -1L)
        {
            INST(5U, Opcode::ReturnVoid);
        }
    }

    EXPECT_TRUE(GetGraph()->RunPass<LivenessAnalyzer>());
    CheckSubsequence(GetGraph()->GetAnalysis<LivenessAnalyzer>().GetLinearizedBlocks(),
                     GetBlocksById(GetGraph(), {0, 2, 4, 3, 1}));

    BB(2U).SwapTrueFalseSuccessors();
    EXPECT_TRUE(GetGraph()->RunPass<LivenessAnalyzer>());
    CheckSubsequence(GetGraph()->GetAnalysis<LivenessAnalyzer>().GetLinearizedBlocks(),
                     GetBlocksById(GetGraph(), {0, 2, 4, 3, 1}));
}

/*
 * Сheck LifeIntervals updating correctness
 */
TEST_F(LivenessAnalyzerTest, LifeIntervals)
{
    LifeIntervals life_inter(GetAllocator());
    life_inter.AppendRange({90U, 100U});
    life_inter.AppendRange({80U, 90U});
    life_inter.AppendRange({40U, 50U});
    life_inter.AppendRange({35U, 40U});
    EXPECT_EQ(life_inter.GetRanges(), LIVE_RANGES_VECTOR({80U, 100U}, {35U, 50U}));

    life_inter.AppendRange({20U, 34U});
    life_inter.StartFrom(30U);
    EXPECT_EQ(life_inter.GetRanges(), LIVE_RANGES_VECTOR({80U, 100U}, {35U, 50U}, {30U, 34U}));

    life_inter.AppendRange({10U, 20U});
    life_inter.AppendGroupRange({10U, 25U});
    EXPECT_EQ(life_inter.GetRanges(), LIVE_RANGES_VECTOR({80U, 100U}, {35U, 50U}, {30U, 34U}, {10U, 25U}));

    life_inter.AppendGroupRange({10U, 79U});
    EXPECT_EQ(life_inter.GetRanges(), LIVE_RANGES_VECTOR({80U, 100U}, {10U, 79U}));

    life_inter.AppendGroupRange({10U, 95U});
    EXPECT_EQ(life_inter.GetRanges(), LIVE_RANGES_VECTOR({10U, 100U}));
}

/*
 * Test Graph:
 *              [0]
 *               |
 *               v
 *        /-----[2]<----\
 *        |      |      |
 *        |      v      |
 *        |     [3]-----/
 *        |
 *        \---->[4]
 *               |
 *               v
 *             [exit]
 *
 *
 * Blocks linear order:
 * ID   LIFE RANGE
 * ---------------
 * 0    [0, 8]
 * 2    [8, 14]
 * 3    [14, 22]
 * 4    [22, 26]
 *
 *
 * Insts linear order:
 * ID   INST(INPUTS)    LIFE NUMBER LIFE INTERVALS
 * ------------------------------------------
 * 0.   Constant        2           [2-22]
 * 1.   Constant        4           [4-8]
 * 2.   Constant        6           [6-24]
 *
 * 3.   Phi (0,7)       8           [8-16][22-24]
 * 4.   Phi (1,8)       8           [8-18]
 * 5.   Cmp (4,0)       10          [10-12]
 * 6.   If    (5)       12          -
 *
 * 7.   Mul (3,4)       16          [16-2?]
 * 8.   Sub (4,0)       18          [18-2?]
 *
 * 9.   Add (2,3)       2?          [2?-2?]
 */
TEST_F(LivenessAnalyzerTest, InstructionsLifetime)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0U, 1U);
        CONSTANT(1U, 10U);
        CONSTANT(2U, 20U);

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(3U, Opcode::Phi).u64().Inputs({{0U, 0U}, {3U, 7U}});
            INST(4U, Opcode::Phi).u64().Inputs({{0U, 1U}, {3U, 8U}});
            INST(5U, Opcode::Compare).b().Inputs(4U, 0U);
            INST(6U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(5U);
        }

        BASIC_BLOCK(3U, 2U)
        {
            INST(7U, Opcode::Mul).u64().Inputs(3U, 4U);
            INST(8U, Opcode::Sub).u64().Inputs(4U, 0U);
        }

        BASIC_BLOCK(4U, -1L)
        {
            INST(9U, Opcode::Add).u64().Inputs(2U, 3U);
            INST(10U, Opcode::ReturnVoid);
        }
    }
    auto liveness_analyzer = &GetGraph()->GetValidAnalysis<LivenessAnalyzer>();

    auto const0 = liveness_analyzer->GetInstLifeIntervals(&INS(0U));
    auto const1 = liveness_analyzer->GetInstLifeIntervals(&INS(1U));
    auto const2 = liveness_analyzer->GetInstLifeIntervals(&INS(2U));
    auto phi0 = liveness_analyzer->GetInstLifeIntervals(&INS(3U));
    auto phi1 = liveness_analyzer->GetInstLifeIntervals(&INS(4U));
    auto cmp = liveness_analyzer->GetInstLifeIntervals(&INS(5U));
    auto mul = liveness_analyzer->GetInstLifeIntervals(&INS(7U));
    auto sub = liveness_analyzer->GetInstLifeIntervals(&INS(8U));
    auto add = liveness_analyzer->GetInstLifeIntervals(&INS(9U));

    auto b0_lifetime = liveness_analyzer->GetBlockLiveRange(&BB(0U));
    auto b2_lifetime = liveness_analyzer->GetBlockLiveRange(&BB(2U));
    auto b3_lifetime = liveness_analyzer->GetBlockLiveRange(&BB(3U));
    auto b4_lifetime = liveness_analyzer->GetBlockLiveRange(&BB(4U));

    EXPECT_EQ(const0->GetRanges()[0U], LiveRange(b0_lifetime.GetBegin() + 2U, b3_lifetime.GetEnd()));
    EXPECT_EQ(const1->GetRanges()[0U], LiveRange(b0_lifetime.GetBegin() + 4U, phi0->GetRanges()[1U].GetBegin()));
    EXPECT_EQ(const2->GetRanges()[0U], LiveRange(b0_lifetime.GetBegin() + 6U, add->GetRanges()[0U].GetBegin()));
    EXPECT_EQ(phi0->GetRanges()[1U], LiveRange(b2_lifetime.GetBegin(), mul->GetRanges()[0U].GetBegin()));
    EXPECT_EQ(phi0->GetRanges()[0U], LiveRange(b4_lifetime.GetBegin(), add->GetRanges()[0U].GetBegin()));
    EXPECT_EQ(phi1->GetRanges()[0U], LiveRange(b2_lifetime.GetBegin(), sub->GetRanges()[0U].GetBegin()));
    EXPECT_EQ(cmp->GetRanges()[0U], LiveRange(b2_lifetime.GetBegin() + 2U, b2_lifetime.GetBegin() + 4U));
    EXPECT_EQ(mul->GetRanges()[0U], LiveRange(b3_lifetime.GetBegin() + 2U, b3_lifetime.GetEnd()));
    EXPECT_EQ(sub->GetRanges()[0U], LiveRange(b3_lifetime.GetBegin() + 4U, b3_lifetime.GetEnd()));
    EXPECT_EQ(add->GetRanges()[0U], LiveRange(b4_lifetime.GetBegin() + 2U, b4_lifetime.GetBegin() + 4U));
}

TEST_F(LivenessAnalyzerTest, LoadStoreArrayDataFlow)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).ref();  // array
        PARAMETER(1U, 1U).s64();  // index
        BASIC_BLOCK(2U, 3U)
        {
            INST(2U, Opcode::SaveState).Inputs(0U, 1U).SrcVregs({0U, 1U});
            INST(3U, Opcode::NullCheck).ref().Inputs(0U, 2U);
            INST(4U, Opcode::LenArray).s32().Inputs(3U);
            INST(5U, Opcode::BoundsCheck).s32().Inputs(4U, 1U, 2U);
            INST(6U, Opcode::LoadArray).u64().Inputs(3U, 5U);
            INST(7U, Opcode::Add).s64().Inputs(6U, 6U);
            INST(8U, Opcode::StoreArray).u64().Inputs(3U, 5U, 7U);
        }
        BASIC_BLOCK(3U, -1L)
        {
            INST(11U, Opcode::Return).u64().Inputs(7U);  // Some return value
        }
    }

    auto liveness_analyzer = &GetGraph()->GetAnalysis<LivenessAnalyzer>();
    liveness_analyzer->Run();

    auto array = liveness_analyzer->GetInstLifeIntervals(&INS(0U));
    auto index = liveness_analyzer->GetInstLifeIntervals(&INS(1U));
    auto null_check = liveness_analyzer->GetInstLifeIntervals(&INS(3U));
    auto len_array = liveness_analyzer->GetInstLifeIntervals(&INS(4U));
    auto bounds_check = liveness_analyzer->GetInstLifeIntervals(&INS(5U));
    auto st_array = liveness_analyzer->GetInstLifeIntervals(&INS(8U));

    auto b0_lifetime = liveness_analyzer->GetBlockLiveRange(&BB(0U));

    EXPECT_EQ(array->GetRanges()[0U], LiveRange(b0_lifetime.GetBegin() + 2U, st_array->GetRanges()[0U].GetBegin()));
    EXPECT_EQ(index->GetRanges()[0U], LiveRange(b0_lifetime.GetBegin() + 4U, st_array->GetRanges()[0U].GetBegin()));

    EXPECT_EQ(null_check->GetRanges()[0U].GetEnd() - null_check->GetRanges()[0U].GetBegin(), 2U);
    EXPECT_EQ(bounds_check->GetRanges()[0U].GetEnd() - bounds_check->GetRanges()[0U].GetBegin(), 2U);
    EXPECT_EQ(len_array->GetRanges()[0U].GetEnd() - len_array->GetRanges()[0U].GetBegin(), 2U);
}

TEST_F(LivenessAnalyzerTest, SaveStateInputs)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).ref();
        PARAMETER(1U, 1U).u32();
        PARAMETER(2U, 2U).u32();

        BASIC_BLOCK(2U, -1L)
        {
            INST(3U, Opcode::SaveState).Inputs(0U, 1U, 2U).SrcVregs({0U, 1U, 2U});
            INST(4U, Opcode::NullCheck).ref().Inputs(0U, 3U);
            INST(5U, Opcode::StoreObject).u32().Inputs(4U, 1U);
            INST(6U, Opcode::ReturnVoid).v0id();
        }
    }
    auto liveness_analyzer = &GetGraph()->GetAnalysis<LivenessAnalyzer>();
    liveness_analyzer->Run();

    auto par0_lifetime = liveness_analyzer->GetInstLifeIntervals(&INS(0U));
    auto par1_lifetime = liveness_analyzer->GetInstLifeIntervals(&INS(1U));
    auto par2_lifetime = liveness_analyzer->GetInstLifeIntervals(&INS(2U));
    auto null_check_lifetime = liveness_analyzer->GetInstLifeIntervals(&INS(4U));
    EXPECT_TRUE(par0_lifetime->GetEnd() == null_check_lifetime->GetEnd());
    EXPECT_TRUE(par1_lifetime->GetEnd() == null_check_lifetime->GetEnd());
    EXPECT_TRUE(par2_lifetime->GetEnd() == null_check_lifetime->GetBegin());
}

/**
 *        [begin]
 *           |
 *          [2]
 *           |
 *          [3]<-------\
 *         /    \      |
 *      [end]   [4]    |
 *               |     |
 *        /---->[5]----/
 *        |      |
 *        \-----[6]
 *
 */
TEST_F(LivenessAnalyzerTest, InnerLoops)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).u32();  // a
        PARAMETER(1U, 1U).u32();  // b
        PARAMETER(2U, 2U).u32();  // c
        CONSTANT(3U, 1U);         // d

        BASIC_BLOCK(2U, 3U)
        {
            INST(5U, Opcode::Mul).u32().Inputs(0U, 1U);  // a * b
            INST(6U, Opcode::Add).u32().Inputs(0U, 1U);  // a + b
        }
        BASIC_BLOCK(3U, 4U, 7U)
        {
            INST(9U, Opcode::Phi).u32().Inputs(2U, 12U);
            INST(10U, Opcode::If).SrcType(DataType::UINT32).CC(CC_LT).Inputs(9U, 5U);  // if c < a * b
        }
        BASIC_BLOCK(4U, 5U)
        {
            INST(11U, Opcode::Add).u32().Inputs(9U, 3U);  // c++
        }
        BASIC_BLOCK(5U, 6U, 3U)
        {
            INST(12U, Opcode::Phi).u32().Inputs(11U, 14U);
            INST(13U, Opcode::If).SrcType(DataType::UINT32).CC(CC_LT).Inputs(12U, 6U);  // if c < a + b
        }
        BASIC_BLOCK(6U, 5U)
        {
            INST(14U, Opcode::Add).u32().Inputs(12U, 3U);  // c++
        }
        BASIC_BLOCK(7U, -1L)
        {
            INST(15U, Opcode::ReturnVoid);
        }
    }

    auto liveness_analyzer = &GetGraph()->GetAnalysis<LivenessAnalyzer>();
    liveness_analyzer->Run();
    auto mul = liveness_analyzer->GetInstLifeIntervals(&INS(5U));
    auto add = liveness_analyzer->GetInstLifeIntervals(&INS(6U));
    auto inner_loop_back = liveness_analyzer->GetBlockLiveRange(&BB(6U));
    EXPECT_EQ(mul->GetEnd(), inner_loop_back.GetEnd());
    EXPECT_EQ(add->GetEnd(), inner_loop_back.GetEnd());
}

TEST_F(LivenessAnalyzerTest, UpdateExistingRanges)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).u32();
        PARAMETER(1U, 1U).u32();

        BASIC_BLOCK(2U, 4U, 3U)
        {
            INST(2U, Opcode::Add).u32().Inputs(0U, 1U);
            INST(3U, Opcode::Compare).b().Inputs(0U, 2U);
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(3U);
        }

        BASIC_BLOCK(3U, -1L)
        {
            INST(5U, Opcode::Cast).u32().SrcType(DataType::BOOL).Inputs(3U);
            INST(6U, Opcode::Return).u32().Inputs(5U);
        }

        BASIC_BLOCK(4U, -1L)
        {
            INST(7U, Opcode::ReturnI).u32().Imm(1U);
        }
    }

    auto la = &GetGraph()->GetAnalysis<LivenessAnalyzer>();
    la->Run();

    auto cmp = la->GetInstLifeIntervals(&INS(3U));
    EXPECT_EQ(cmp->GetRanges().size(), 2U);

    auto first_interval = cmp->GetRanges().back();
    auto add = la->GetInstLifeIntervals(&INS(2U));
    EXPECT_EQ(first_interval.GetBegin(), add->GetEnd());
    EXPECT_EQ(first_interval.GetEnd(), la->GetBlockLiveRange(&BB(2U)).GetEnd());

    auto second_interval = cmp->GetRanges().front();
    auto cast = la->GetInstLifeIntervals(&INS(5U));
    EXPECT_EQ(second_interval.GetBegin(), la->GetBlockLiveRange(&BB(3U)).GetBegin());
    EXPECT_EQ(second_interval.GetEnd(), cast->GetBegin());
}

TEST_F(LivenessAnalyzerTest, ReturnInlinedLiveness)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).s32();
        PARAMETER(1U, 1U).s32();
        PARAMETER(20U, 2U).s32();

        BASIC_BLOCK(2U, -1L)
        {
            INST(2U, Opcode::SaveState).Inputs(20U).SrcVregs({0U});
            INST(3U, Opcode::CallStatic).v0id().Inlined().InputsAutoType(2U);
            INST(4U, Opcode::ReturnInlined).s32().Inputs(2U);
            INST(5U, Opcode::SaveState).Inputs(0U, 1U).SrcVregs({0U, 1U});
            INST(6U, Opcode::CallStatic).v0id().Inlined().InputsAutoType(5U);
            INST(7U, Opcode::CallStatic).v0id().Inlined().InputsAutoType(5U);
            INST(8U, Opcode::SaveStateDeoptimize).Inputs(6U, 7U).SrcVregs({0U, 1U});
            INST(9U, Opcode::ReturnInlined).s32().Inputs(5U);
            INST(10U, Opcode::ReturnInlined).s32().Inputs(5U);
            INST(13U, Opcode::NOP);
            INST(14U, Opcode::NOP);
            INST(11U, Opcode::Deoptimize).Inputs(8U);
            INST(12U, Opcode::ReturnVoid).v0id();
        }
    }
    INS(10U).CastToReturnInlined()->SetExtendedLiveness();

    auto la = &GetGraph()->GetAnalysis<LivenessAnalyzer>();
    la->Run();
    auto par0_lifetime = la->GetInstLifeIntervals(&INS(0U));
    auto par1_lifetime = la->GetInstLifeIntervals(&INS(1U));
    auto par2_lifetime = la->GetInstLifeIntervals(&INS(20U));
    auto deopt_lifetime = la->GetInstLifeIntervals(&INS(11U));
    // 5.SaveState's inputs' liveness should be propagated up to 11.Deoptimize
    EXPECT_GE(par0_lifetime->GetEnd(), deopt_lifetime->GetBegin());
    EXPECT_GE(par1_lifetime->GetEnd(), deopt_lifetime->GetBegin());
    // 2.SaveState's input's liveness should not be propagated
    EXPECT_LT(par2_lifetime->GetEnd(), deopt_lifetime->GetBegin());
}

TEST_F(LivenessAnalyzerTest, LookupInstByLifeNumber)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(2U, Opcode::Compare).b().SrcType(DataType::Type::UINT64).Inputs(0U, 1U);
            INST(3U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(2U);
        }

        BASIC_BLOCK(3U, 5U)
        {
            INST(4U, Opcode::Add).u64().Inputs(0U, 1U);
        }

        BASIC_BLOCK(4U, 5U)
        {
            INST(5U, Opcode::Sub).u64().Inputs(0U, 1U);
        }

        BASIC_BLOCK(5U, -1L)
        {
            INST(6U, Opcode::Phi).u64().Inputs(4U, 5U);
            INST(7U, Opcode::Return).u64().Inputs(6U);
        }
    }

    auto &la = GetGraph()->GetAnalysis<LivenessAnalyzer>();
    la.Run();

    EXPECT_EQ(la.GetInstByLifeNumber(la.GetInstLifeIntervals(&INS(4U))->GetBegin()), &INS(4U));
    EXPECT_EQ(la.GetInstByLifeNumber(la.GetInstLifeIntervals(&INS(4U))->GetBegin() + 1U), &INS(4U));
    EXPECT_EQ(la.GetInstByLifeNumber(la.GetInstLifeIntervals(&INS(6U))->GetBegin()), nullptr);
}

TEST_F(LivenessAnalyzerTest, PhiDataFlowInput)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0U, nullptr).ref();
        PARAMETER(1U, 0U).ref();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(5U, Opcode::SaveState).Inputs(0U, 1U).SrcVregs({0U, 1U});
            INST(6U, Opcode::NullCheck).ref().Inputs(1U, 5U);
            INST(7U, Opcode::LoadObject).s32().Inputs(6U);
            INST(8U, Opcode::IfImm).SrcType(compiler::DataType::INT32).CC(compiler::CC_NE).Imm(0U).Inputs(7U);
        }
        BASIC_BLOCK(3U, 5U) {}
        BASIC_BLOCK(4U, 5U) {}
        BASIC_BLOCK(5U, 1U)
        {
            INST(9U, Opcode::Phi).ref().Inputs(0U, 6U);
            INST(10U, Opcode::Return).ref().Inputs(9U);
        }
    }

    auto &la = GetGraph()->GetAnalysis<LivenessAnalyzer>();
    la.Run();
    auto par0_lifetime = la.GetInstLifeIntervals(&INS(1U));
    auto phi_lifetime = la.GetInstLifeIntervals(&INS(9U));
    EXPECT_EQ(par0_lifetime->GetEnd(), phi_lifetime->GetBegin());
}

TEST_F(LivenessAnalyzerTest, CatchProcessing)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).u64();

        BASIC_BLOCK(2U, 3U)
        {
            INST(1U, Opcode::Try).CatchTypeIds({0x0U});
        }

        BASIC_BLOCK(3U, 5U)
        {
            INST(2U, Opcode::Mul).u64().Inputs(0U, 0U);
            INST(3U, Opcode::Mul).u64().Inputs(0U, 2U);
        }

        BASIC_BLOCK(5U, 6U, 4U) {}  // try-end

        BASIC_BLOCK(4U, -1L)
        {
            INST(5U, Opcode::Return).u64().Inputs(2U);
        }

        BASIC_BLOCK(6U, -1L)
        {
            INST(4U, Opcode::Return).u64().Inputs(3U);
        }
    }
    BB(2U).SetTryId(0U);
    BB(3U).SetTryId(0U);
    BB(5U).SetTryId(0U);

    GetGraph()->AppendThrowableInst(&INS(3U), &BB(5U));
    INS(1U).CastToTry()->SetTryEndBlock(&BB(5U));

    auto &la = GetGraph()->GetAnalysis<LivenessAnalyzer>();
    la.Run();

    EXPECT_EQ(la.GetInstByLifeNumber(la.GetInstLifeIntervals(&INS(2U))->GetBegin()), &INS(2U));
    EXPECT_EQ(la.GetInstByLifeNumber(la.GetInstLifeIntervals(&INS(3U))->GetBegin()), &INS(3U));
}

TEST_F(LivenessAnalyzerTest, FirstIntersection)
{
    // li:      [10-20]         [30-40]
    // other:           [21-25]         [45-100]
    // intersection: INVALID_LIFE_NUMBER
    LifeIntervals li(GetAllocator());
    li.AppendRange({30U, 40U});
    li.AppendRange({10U, 20U});
    LifeIntervals other_li(GetAllocator());
    other_li.AppendRange({45U, 100U});
    other_li.AppendRange({21U, 25U});
    EXPECT_EQ(li.GetFirstIntersectionWith(&other_li), INVALID_LIFE_NUMBER);

    // li:             [21-25] [30-40]
    // other:   [10-20]                 [45-100]
    // intersection: INVALID_LIFE_NUMBER
    li.Clear();
    li.AppendRange({30U, 40U});
    li.AppendRange({21U, 25U});
    other_li.Clear();
    other_li.AppendRange({45U, 100U});
    other_li.AppendRange({10U, 20U});
    EXPECT_EQ(li.GetFirstIntersectionWith(&other_li), INVALID_LIFE_NUMBER);

    // li:      [10-20]         [30-40]
    // other:       [15-25]         [35-100]
    // intersection: INVALID_LIFE_NUMBER
    li.Clear();
    li.AppendRange({30U, 40U});
    li.AppendRange({10U, 20U});
    other_li.Clear();
    other_li.AppendRange({35U, 100U});
    other_li.AppendRange({15U, 25U});
    EXPECT_EQ(li.GetFirstIntersectionWith(&other_li), LifeNumber(15U));

    // li:          [15-25]         [35-100]
    // other:   [10-20]         [30-40]
    // intersection: INVALID_LIFE_NUMBER
    li.Clear();
    li.AppendRange({35U, 100U});
    li.AppendRange({15U, 25U});
    other_li.Clear();
    other_li.AppendRange({30U, 40U});
    other_li.AppendRange({10U, 20U});
    EXPECT_EQ(li.GetFirstIntersectionWith(&other_li), LifeNumber(15U));

    // li:               [25-35] [45    -    100]
    // other:   [10-20]              [50-60]
    // intersection: INVALID_LIFE_NUMBER
    li.Clear();
    li.AppendRange({45U, 100U});
    li.AppendRange({25U, 35U});
    other_li.Clear();
    other_li.AppendRange({50U, 60U});
    other_li.AppendRange({10U, 20U});
    EXPECT_EQ(li.GetFirstIntersectionWith(&other_li), LifeNumber(50U));

    // li:      [0-10]
    // other:     [6-12]
    // seach_from:   8
    // intersection: 8
    li.Clear();
    li.AppendRange({0U, 10U});
    other_li.Clear();
    other_li.AppendRange({6U, 12U});
    EXPECT_EQ(li.GetFirstIntersectionWith(&other_li, 8U), LifeNumber(8U));

    // li:      [0-10]     [20-30]
    // other:   [0-2]   [18-24]
    // search_from: 2
    // intersection: 20
    li.Clear();
    li.AppendRange({20U, 30U});
    li.AppendRange({0U, 10U});
    other_li.Clear();
    other_li.AppendRange({18U, 24U});
    other_li.AppendRange({0U, 2U});
    EXPECT_EQ(li.GetFirstIntersectionWith(&other_li, 2U), LifeNumber(20U));

    // li:         [10-20]
    // other:    [8-30]
    // search_from: 18
    // intersection: 18
    li.Clear();
    li.AppendRange({10U, 20U});
    other_li.Clear();
    other_li.AppendRange({8U, 30U});
    EXPECT_EQ(li.GetFirstIntersectionWith(&other_li, 18U), LifeNumber(18U));

    // li:      [0-10]  [20-22]       [24-26]
    // other:   [0-2]          [22-24]
    // search_from: 12
    // intersection: INVALID_LIFE_NUMBER
    li.Clear();
    li.AppendRange({24U, 26U});
    li.AppendRange({20U, 22U});
    li.AppendRange({0U, 10U});
    other_li.Clear();
    other_li.AppendRange({22U, 24U});
    other_li.AppendRange({0U, 2U});
    EXPECT_EQ(li.GetFirstIntersectionWith(&other_li, 12U), INVALID_LIFE_NUMBER);
}

TEST_F(LivenessAnalyzerTest, IntersectionExistence)
{
    // interval: [10--------40]
    // physical: [10-11]
    // no intersection: physical's [10-11] range was created for the interval's instruction to block dst register
    LifeIntervals interval(GetAllocator());
    interval.AppendRange({10U, 40U});

    LifeIntervals physical(GetAllocator());
    physical.SetPhysicalReg(0U, DataType::INT64);  // Make interval physical
    physical.AppendRange({10U, 11U});
    EXPECT_FALSE(interval.IntersectsWith<true>(&physical));

    // interval: [10----------------40]
    // physical: [10-11]   [20-21]
    // intersection at point 20
    interval.Clear();
    interval.AppendRange({10U, 40U});

    physical.Clear();
    physical.AppendRange({20U, 21U});
    physical.AppendRange({10U, 11U});
    EXPECT_TRUE(interval.IntersectsWith<true>(&physical));
}

TEST_F(LivenessAnalyzerTest, NextUsePositions)
{
    LifeIntervals li(GetAllocator());
    li.AppendRange({0U, 100U});
    li.AddUsePosition(20U);
    li.AddUsePosition(50U);
    li.AddUsePosition(75U);
    li.AddUsePosition(100U);
    li.Finalize();

    EXPECT_EQ(li.GetNextUsage(0U), 20U);
    EXPECT_EQ(li.GetNextUsage(20U), 20U);
    EXPECT_EQ(li.GetNextUsage(30U), 50U);
    EXPECT_EQ(li.GetNextUsage(50U), 50U);
    EXPECT_EQ(li.GetNextUsage(70U), 75U);
    EXPECT_EQ(li.GetNextUsage(75U), 75U);
    EXPECT_EQ(li.GetNextUsage(90U), 100U);
    EXPECT_EQ(li.GetNextUsage(100U), 100U);
    EXPECT_EQ(li.GetNextUsage(150U), INVALID_LIFE_NUMBER);
}

TEST_F(LivenessAnalyzerTest, NoUsageUntil)
{
    LifeIntervals li(GetAllocator());
    li.AppendRange({0U, 100U});
    li.AddUsePosition(20U);
    li.AddUsePosition(50U);
    li.Finalize();
    EXPECT_TRUE(li.NoUsageUntil(0U));
    EXPECT_TRUE(li.NoUsageUntil(19U));
    EXPECT_FALSE(li.NoUsageUntil(20U));
    EXPECT_FALSE(li.NoUsageUntil(21U));
    EXPECT_FALSE(li.NoUsageUntil(100U));
}

TEST_F(LivenessAnalyzerTest, SplitUsePositions)
{
    LifeIntervals li(GetAllocator());
    li.AppendRange({0U, 200U});
    li.AddUsePosition(20U);
    li.AddUsePosition(50U);
    li.AddUsePosition(75U);
    li.AddUsePosition(100U);
    li.Finalize();

    auto split = li.SplitAt(50U, GetAllocator());
    EXPECT_THAT(li.GetUsePositions(), ::testing::ElementsAre(20U));
    EXPECT_THAT(split->GetUsePositions(), ::testing::ElementsAre(50U, 75U, 100U));

    auto next_spit = split->SplitAt(150U, GetAllocator());
    EXPECT_TRUE(next_spit->GetUsePositions().empty());
    EXPECT_THAT(split->GetUsePositions(), ::testing::ElementsAre(50U, 75U, 100U));
}

TEST_F(LivenessAnalyzerTest, PropagateLivenessForImplicitNullCheckSaveStateInputs)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).ref();
        PARAMETER(1U, 1U).s32();

        BASIC_BLOCK(2U, -1L)
        {
            INST(2U, Opcode::SaveState).Inputs(0U, 1U).SrcVregs({0U, 1U});
            INST(3U, Opcode::NullCheck).ref().Inputs(0U, 2U);
            INST(4U, Opcode::AddI).s32().Imm(1U).Inputs(1U);
            INST(5U, Opcode::LenArray).s32().Inputs(3U);
            INST(6U, Opcode::Add).s32().Inputs(4U, 5U);
            INST(7U, Opcode::Return).s32().Inputs(6U);
        }
    }
    INS(3U).CastToNullCheck()->SetImplicit(true);

    auto &la = GetGraph()->GetAnalysis<LivenessAnalyzer>();
    ASSERT_TRUE(la.Run());

    auto param = la.GetInstLifeIntervals(&INS(1U));
    auto len = la.GetInstLifeIntervals(&INS(5U));
    ASSERT_GE(param->GetEnd(), len->GetBegin());
}

TEST_F(LivenessAnalyzerTest, PropagateLivenessForExplicitNullCheckSaveStateInputs)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).ref();
        PARAMETER(1U, 1U).s32();

        BASIC_BLOCK(2U, -1L)
        {
            INST(2U, Opcode::SaveState).Inputs(0U, 1U).SrcVregs({0U, 1U});
            INST(3U, Opcode::NullCheck).ref().Inputs(0U, 2U);
            INST(4U, Opcode::AddI).s32().Imm(1U).Inputs(1U);
            INST(5U, Opcode::LenArray).s32().Inputs(3U);
            INST(6U, Opcode::Add).s32().Inputs(4U, 5U);
            INST(7U, Opcode::Return).s32().Inputs(6U);
        }
    }
    INS(3U).CastToNullCheck()->SetImplicit(false);

    auto &la = GetGraph()->GetAnalysis<LivenessAnalyzer>();
    ASSERT_TRUE(la.Run());

    auto param = la.GetInstLifeIntervals(&INS(1U));
    auto addi = la.GetInstLifeIntervals(&INS(4U));
    ASSERT_EQ(param->GetEnd(), addi->GetBegin());
}

TEST_F(LivenessAnalyzerTest, NullCheckWithoutUsers)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).ref();
        PARAMETER(1U, 1U).s32();

        BASIC_BLOCK(2U, -1L)
        {
            INST(20U, Opcode::SaveState).NoVregs();
            INST(2U, Opcode::CallStatic).ref().InputsAutoType(0U, 20U);
            INST(3U, Opcode::SaveState).Inputs(2U).SrcVregs({0U});
            INST(4U, Opcode::NullCheck).ref().Inputs(2U, 3U);
            INST(5U, Opcode::Return).s32().Inputs(1U);
        }
    }
    BB(2U).SetTry(true);
    INS(4U).CastToNullCheck()->SetImplicit(true);

    auto &la = GetGraph()->GetAnalysis<LivenessAnalyzer>();
    ASSERT_TRUE(la.Run());

    auto call = la.GetInstLifeIntervals(&INS(2U));
    auto null_check = la.GetInstLifeIntervals(&INS(4U));
    ASSERT_EQ(call->GetEnd(), null_check->GetBegin() + 1U);
}

TEST_F(LivenessAnalyzerTest, UseHints)
{
    if (GetGraph()->GetCallingConvention() == nullptr) {
        return;
    }
    GRAPH(GetGraph())
    {
        CONSTANT(0U, 0U).s32();

        BASIC_BLOCK(2U, -1L)
        {
            INST(1U, Opcode::SaveState).NoVregs();
            INST(2U, Opcode::CallStatic).s32().InputsAutoType(0U, 1U);
            INST(3U, Opcode::Add).s32().Inputs(2U, 0U);
            INST(4U, Opcode::SaveState).NoVregs();
            INST(5U, Opcode::CallStatic).s32().InputsAutoType(3U, 0U, 4U);
            INST(6U, Opcode::Add).s32().Inputs(2U, 0U);
            INST(7U, Opcode::Return).s32().Inputs(6U);
        }
    }

    auto &la = GetGraph()->GetAnalysis<LivenessAnalyzer>();
    ASSERT_TRUE(la.Run());

    auto call0 = la.GetInstLifeIntervals(&INS(2U));
    auto add0 = la.GetInstLifeIntervals(&INS(3U));
    auto call1 = la.GetInstLifeIntervals(&INS(5U));
    auto add1 = la.GetInstLifeIntervals(&INS(6U));
    auto constant = &INS(0U);
    auto &ut = la.GetUseTable();

    EXPECT_TRUE(ut.HasUseOnFixedLocation(constant, call0->GetBegin()));
    EXPECT_TRUE(ut.HasUseOnFixedLocation(constant, call1->GetBegin()));
    EXPECT_FALSE(ut.HasUseOnFixedLocation(constant, add0->GetBegin()));
    EXPECT_FALSE(ut.HasUseOnFixedLocation(constant, add1->GetBegin()));
    EXPECT_EQ(ut.GetNextUseOnFixedLocation(constant, call0->GetBegin()), INS(2U).GetLocation(0U).GetRegister());
    EXPECT_EQ(ut.GetNextUseOnFixedLocation(constant, call1->GetBegin()), INS(5U).GetLocation(1U).GetRegister());
}
// NOLINTEND(readability-magic-numbers)

}  // namespace panda::compiler
