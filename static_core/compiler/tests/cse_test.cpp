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

#include "macros.h"
#include "unit_test.h"
#include "optimizer/optimizations/cse.h"

namespace panda::compiler {
class CSETest : public GraphTest {
public:
    CSETest() : is_cse_enable_default_(OPTIONS.IsCompilerCse())
    {
        OPTIONS.SetCompilerCse(true);
    }

    ~CSETest() override
    {
        OPTIONS.SetCompilerCse(is_cse_enable_default_);
    }

    NO_COPY_SEMANTIC(CSETest);
    NO_MOVE_SEMANTIC(CSETest);

private:
    bool is_cse_enable_default_;
};

// NOLINTBEGIN(readability-magic-numbers)
TEST_F(CSETest, CSETestApply1)
{
    // Remove duplicate arithmetic instructions in one basicblock
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).f64();
        PARAMETER(3U, 3U).f64();
        PARAMETER(4U, 4U).f32();
        PARAMETER(5U, 5U).f32();

        BASIC_BLOCK(2U, -1L)
        {
            INST(6U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(7U, Opcode::Sub).u32().Inputs(1U, 0U);
            INST(8U, Opcode::Mul).f32().Inputs(4U, 5U);
            INST(9U, Opcode::Div).f64().Inputs(3U, 2U);

            INST(10U, Opcode::Sub).u32().Inputs(1U, 0U);
            INST(11U, Opcode::Div).f64().Inputs(3U, 2U);
            INST(12U, Opcode::Mul).f32().Inputs(4U, 5U);
            INST(13U, Opcode::Add).u64().Inputs(0U, 1U);

            INST(14U, Opcode::Mod).u64().Inputs(0U, 1U);
            INST(15U, Opcode::Min).u64().Inputs(0U, 1U);
            INST(16U, Opcode::Max).u64().Inputs(0U, 1U);
            INST(17U, Opcode::Shl).u64().Inputs(0U, 1U);
            INST(18U, Opcode::Shr).u64().Inputs(0U, 1U);
            INST(19U, Opcode::AShr).u64().Inputs(0U, 1U);
            INST(20U, Opcode::And).b().Inputs(0U, 1U);
            INST(21U, Opcode::Or).b().Inputs(0U, 1U);
            INST(22U, Opcode::Xor).b().Inputs(0U, 1U);

            INST(23U, Opcode::Mod).u64().Inputs(0U, 1U);
            INST(24U, Opcode::Min).u64().Inputs(0U, 1U);
            INST(25U, Opcode::Max).u64().Inputs(0U, 1U);
            INST(26U, Opcode::Shl).u64().Inputs(0U, 1U);
            INST(27U, Opcode::Shr).u64().Inputs(0U, 1U);
            INST(28U, Opcode::AShr).u64().Inputs(0U, 1U);
            INST(29U, Opcode::And).b().Inputs(0U, 1U);
            INST(30U, Opcode::Or).b().Inputs(0U, 1U);
            INST(31U, Opcode::Xor).b().Inputs(0U, 1U);
            INST(35U, Opcode::SaveState).NoVregs();
            INST(32U, Opcode::CallStatic)
                .b()
                .InputsAutoType(6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
                                29U, 30U, 31U, 35U);
            INST(33U, Opcode::ReturnVoid);
        }
    }

    // delete insts 10, 11, 12, 13, 23, 24, 25, 26, 27, 28, 29, 30, 31
    Graph *graph_csed = CreateEmptyGraph();
    GRAPH(graph_csed)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).f64();
        PARAMETER(3U, 3U).f64();
        PARAMETER(4U, 4U).f32();
        PARAMETER(5U, 5U).f32();

        BASIC_BLOCK(2U, -1L)
        {
            INST(6U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(7U, Opcode::Sub).u32().Inputs(1U, 0U);
            INST(8U, Opcode::Mul).f32().Inputs(4U, 5U);
            INST(9U, Opcode::Div).f64().Inputs(3U, 2U);

            INST(14U, Opcode::Mod).u64().Inputs(0U, 1U);
            INST(15U, Opcode::Min).u64().Inputs(0U, 1U);
            INST(16U, Opcode::Max).u64().Inputs(0U, 1U);
            INST(17U, Opcode::Shl).u64().Inputs(0U, 1U);
            INST(18U, Opcode::Shr).u64().Inputs(0U, 1U);
            INST(19U, Opcode::AShr).u64().Inputs(0U, 1U);
            INST(20U, Opcode::And).b().Inputs(0U, 1U);
            INST(21U, Opcode::Or).b().Inputs(0U, 1U);
            INST(22U, Opcode::Xor).b().Inputs(0U, 1U);
            INST(35U, Opcode::SaveState).NoVregs();
            INST(32U, Opcode::CallStatic)
                .b()
                .InputsAutoType(6, 7, 8, 9, 7, 9, 8, 6, 14, 15, 16, 17, 18, 19, 20, 21, 22, 14, 15, 16, 17, 18, 19, 20,
                                21U, 22U, 35U);
            INST(33U, Opcode::ReturnVoid);
        }
    }

    GetGraph()->RunPass<Cse>();
    GraphChecker(GetGraph()).Check();
    ASSERT_TRUE(GraphComparator().Compare(GetGraph(), graph_csed));
}

TEST_F(CSETest, CSETestApply2)
{
    // Remove duplicate arithmetic instructions along dominator tree
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).s32();
        PARAMETER(1U, 1U).s32();
        PARAMETER(2U, 2U).f64();
        PARAMETER(3U, 3U).f64();
        PARAMETER(4U, 4U).f32();
        PARAMETER(5U, 5U).f32();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(6U, Opcode::Add).s32().Inputs(0U, 1U);
            INST(7U, Opcode::Sub).s32().Inputs(1U, 0U);
            INST(8U, Opcode::Mul).f32().Inputs(4U, 5U);
            INST(9U, Opcode::Div).f64().Inputs(3U, 2U);
            INST(35U, Opcode::SaveState).NoVregs();
            INST(21U, Opcode::CallStatic).b().InputsAutoType(6U, 7U, 8U, 9U, 35U);
            INST(10U, Opcode::Compare).b().SrcType(DataType::Type::INT32).Inputs(0U, 1U);
            INST(11U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(10U);
        }
        BASIC_BLOCK(3U, 5U)
        {
            INST(12U, Opcode::Add).s32().Inputs(0U, 1U);
            INST(13U, Opcode::Add).s32().Inputs(12U, 1U);
        }

        BASIC_BLOCK(4U, 5U)
        {
            INST(14U, Opcode::Sub).s32().Inputs(1U, 0U);
            INST(15U, Opcode::Sub).s32().Inputs(14U, 1U);
            INST(16U, Opcode::Mul).f32().Inputs(4U, 5U);
            INST(17U, Opcode::Mul).f32().Inputs(16U, 5U);
        }
        BASIC_BLOCK(5U, -1L)
        {
            INST(18U, Opcode::Div).f64().Inputs(3U, 2U);
            INST(19U, Opcode::Div).f64().Inputs(18U, 3U);
            INST(20U, Opcode::ReturnVoid);
        }
    }

    // Delete Insts 12, 14, 16, 18
    Graph *graph_csed = CreateEmptyGraph();
    GRAPH(graph_csed)
    {
        PARAMETER(0U, 0U).s32();
        PARAMETER(1U, 1U).s32();
        PARAMETER(2U, 2U).f64();
        PARAMETER(3U, 3U).f64();
        PARAMETER(4U, 4U).f32();
        PARAMETER(5U, 5U).f32();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(6U, Opcode::Add).s32().Inputs(0U, 1U);
            INST(7U, Opcode::Sub).s32().Inputs(1U, 0U);
            INST(8U, Opcode::Mul).f32().Inputs(4U, 5U);
            INST(9U, Opcode::Div).f64().Inputs(3U, 2U);
            INST(35U, Opcode::SaveState).NoVregs();
            INST(21U, Opcode::CallStatic).b().InputsAutoType(6U, 7U, 8U, 9U, 35U);
            INST(10U, Opcode::Compare).b().SrcType(DataType::Type::INT32).Inputs(0U, 1U);
            INST(11U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(10U);
        }
        BASIC_BLOCK(3U, 5U)
        {
            INST(13U, Opcode::Add).s32().Inputs(6U, 1U);
        }

        BASIC_BLOCK(4U, 5U)
        {
            INST(15U, Opcode::Sub).s32().Inputs(7U, 1U);
            INST(17U, Opcode::Mul).f32().Inputs(8U, 5U);
        }
        BASIC_BLOCK(5U, -1L)
        {
            INST(19U, Opcode::Div).f64().Inputs(9U, 3U);
            INST(20U, Opcode::ReturnVoid);
        }
    }

    ASSERT_TRUE(GetGraph()->RunPass<Cse>());
    GraphChecker(GetGraph()).Check();
    ASSERT_TRUE(GraphComparator().Compare(GetGraph(), graph_csed));
}

TEST_F(CSETest, CSETestApply3)
{
    // use Phi to replace the arithmetic instruction which has appeared in the two preds of its block.
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).f64();
        PARAMETER(3U, 3U).f64();
        PARAMETER(4U, 4U).f32();
        PARAMETER(5U, 5U).f32();
        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(6U, Opcode::Compare).b().SrcType(DataType::Type::UINT64).Inputs(0U, 1U);
            INST(7U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(6U);
        }
        BASIC_BLOCK(3U, 5U)
        {
            INST(8U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(9U, Opcode::Sub).u32().Inputs(1U, 0U);
            INST(10U, Opcode::Mul).f32().Inputs(4U, 5U);
            INST(11U, Opcode::Div).f64().Inputs(3U, 2U);

            INST(22U, Opcode::Add).u64().Inputs(8U, 1U);
            INST(23U, Opcode::Sub).u32().Inputs(9U, 0U);
            INST(24U, Opcode::Mul).f32().Inputs(10U, 5U);
            INST(25U, Opcode::Div).f64().Inputs(11U, 2U);
        }
        BASIC_BLOCK(4U, 5U)
        {
            INST(12U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(13U, Opcode::Sub).u32().Inputs(1U, 0U);
            INST(14U, Opcode::Mul).f32().Inputs(4U, 5U);
            INST(15U, Opcode::Div).f64().Inputs(3U, 2U);

            INST(26U, Opcode::Add).u64().Inputs(12U, 1U);
            INST(27U, Opcode::Sub).u32().Inputs(13U, 0U);
            INST(28U, Opcode::Mul).f32().Inputs(14U, 5U);
            INST(29U, Opcode::Div).f64().Inputs(15U, 2U);
        }
        BASIC_BLOCK(5U, -1L)
        {
            INST(16U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(17U, Opcode::Sub).u32().Inputs(1U, 0U);
            INST(18U, Opcode::Mul).f32().Inputs(4U, 5U);
            INST(19U, Opcode::Div).f64().Inputs(3U, 2U);
            INST(35U, Opcode::SaveState).NoVregs();
            INST(20U, Opcode::CallStatic).b().InputsAutoType(16U, 17U, 18U, 19U, 35U);
            INST(21U, Opcode::ReturnVoid);
        }
    }

    // Add Phi 30, 31, 32, 33; Delete Inst 16, 17, 18, 19
    Graph *graph_csed = CreateEmptyGraph();
    GRAPH(graph_csed)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).f64();
        PARAMETER(3U, 3U).f64();
        PARAMETER(4U, 4U).f32();
        PARAMETER(5U, 5U).f32();
        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(6U, Opcode::Compare).b().SrcType(DataType::Type::UINT64).Inputs(0U, 1U);
            INST(7U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(6U);
        }
        BASIC_BLOCK(3U, 5U)
        {
            INST(8U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(9U, Opcode::Sub).u32().Inputs(1U, 0U);
            INST(10U, Opcode::Mul).f32().Inputs(4U, 5U);
            INST(11U, Opcode::Div).f64().Inputs(3U, 2U);

            INST(22U, Opcode::Add).u64().Inputs(8U, 1U);
            INST(23U, Opcode::Sub).u32().Inputs(9U, 0U);
            INST(24U, Opcode::Mul).f32().Inputs(10U, 5U);
            INST(25U, Opcode::Div).f64().Inputs(11U, 2U);
        }
        BASIC_BLOCK(4U, 5U)
        {
            INST(12U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(13U, Opcode::Sub).u32().Inputs(1U, 0U);
            INST(14U, Opcode::Mul).f32().Inputs(4U, 5U);
            INST(15U, Opcode::Div).f64().Inputs(3U, 2U);

            INST(26U, Opcode::Add).u64().Inputs(12U, 1U);
            INST(27U, Opcode::Sub).u32().Inputs(13U, 0U);
            INST(28U, Opcode::Mul).f32().Inputs(14U, 5U);
            INST(29U, Opcode::Div).f64().Inputs(15U, 2U);
        }
        BASIC_BLOCK(5U, -1L)
        {
            INST(30U, Opcode::Phi).u64().Inputs({{3U, 8U}, {4U, 12U}});
            INST(31U, Opcode::Phi).u32().Inputs({{3U, 9U}, {4U, 13U}});
            INST(32U, Opcode::Phi).f32().Inputs({{3U, 10U}, {4U, 14U}});
            INST(33U, Opcode::Phi).f64().Inputs({{3U, 11U}, {4U, 15U}});
            INST(35U, Opcode::SaveState).NoVregs();
            INST(20U, Opcode::CallStatic).b().InputsAutoType(30U, 31U, 32U, 33U, 35U);
            INST(21U, Opcode::ReturnVoid);
        }
    }

    ASSERT_TRUE(GetGraph()->RunPass<Cse>());
    GraphChecker(GetGraph()).Check();
    ASSERT_TRUE(GraphComparator().Compare(GetGraph(), graph_csed));
}

TEST_F(CSETest, CSETestApply4)
{
    // Remove duplicate arithmetic instructions in one basicblock (commutative)
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).f64();
        PARAMETER(3U, 3U).f64();
        PARAMETER(4U, 4U).f32();
        PARAMETER(5U, 5U).f32();

        BASIC_BLOCK(2U, -1L)
        {
            INST(6U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(7U, Opcode::Mul).f32().Inputs(4U, 5U);

            INST(8U, Opcode::Mul).f32().Inputs(5U, 4U);
            INST(9U, Opcode::Add).u64().Inputs(1U, 0U);

            INST(10U, Opcode::Min).u64().Inputs(0U, 1U);
            INST(11U, Opcode::Max).u64().Inputs(0U, 1U);
            INST(12U, Opcode::And).b().Inputs(0U, 1U);
            INST(13U, Opcode::Or).b().Inputs(0U, 1U);
            INST(14U, Opcode::Xor).b().Inputs(0U, 1U);

            INST(15U, Opcode::Min).u64().Inputs(1U, 0U);
            INST(16U, Opcode::Max).u64().Inputs(1U, 0U);
            INST(17U, Opcode::And).b().Inputs(1U, 0U);
            INST(18U, Opcode::Or).b().Inputs(1U, 0U);
            INST(19U, Opcode::Xor).b().Inputs(1U, 0U);
            INST(35U, Opcode::SaveState).NoVregs();
            INST(20U, Opcode::CallStatic)
                .b()
                .InputsAutoType(6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U, 18U, 19U, 35U);
            INST(21U, Opcode::ReturnVoid);
        }
    }

    // delete insts 8, 9, 15, 16, 17, 18, 19
    Graph *graph_csed = CreateEmptyGraph();
    GRAPH(graph_csed)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).f64();
        PARAMETER(3U, 3U).f64();
        PARAMETER(4U, 4U).f32();
        PARAMETER(5U, 5U).f32();

        BASIC_BLOCK(2U, -1L)
        {
            INST(6U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(7U, Opcode::Mul).f32().Inputs(4U, 5U);

            INST(10U, Opcode::Min).u64().Inputs(0U, 1U);
            INST(11U, Opcode::Max).u64().Inputs(0U, 1U);
            INST(12U, Opcode::And).b().Inputs(0U, 1U);
            INST(13U, Opcode::Or).b().Inputs(0U, 1U);
            INST(14U, Opcode::Xor).b().Inputs(0U, 1U);
            INST(35U, Opcode::SaveState).NoVregs();
            INST(20U, Opcode::CallStatic)
                .b()
                .InputsAutoType(6U, 7U, 7U, 6U, 10U, 11U, 12U, 13U, 14U, 10U, 11U, 12U, 13U, 14U, 35U);
            INST(21U, Opcode::ReturnVoid);
        }
    }

    GetGraph()->RunPass<Cse>();
    GraphChecker(GetGraph()).Check();
    ASSERT_TRUE(GraphComparator().Compare(GetGraph(), graph_csed));
}

TEST_F(CSETest, CSETestApply5)
{
    // Remove duplicate arithmetic instructions along dominator tree(commutative)
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).s32();
        PARAMETER(1U, 1U).s32();
        PARAMETER(2U, 2U).f64();
        PARAMETER(3U, 3U).f64();
        PARAMETER(4U, 4U).f32();
        PARAMETER(5U, 5U).f32();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(6U, Opcode::Add).s32().Inputs(0U, 1U);
            INST(7U, Opcode::Max).s32().Inputs(1U, 0U);
            INST(8U, Opcode::Mul).f32().Inputs(4U, 5U);
            INST(9U, Opcode::And).b().Inputs(0U, 1U);
            INST(35U, Opcode::SaveState).NoVregs();
            INST(21U, Opcode::CallStatic).b().InputsAutoType(6U, 7U, 8U, 9U, 35U);
            INST(10U, Opcode::Compare).b().SrcType(DataType::Type::INT32).Inputs(0U, 1U);
            INST(11U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(10U);
        }
        BASIC_BLOCK(3U, 5U)
        {
            INST(12U, Opcode::Add).s32().Inputs(1U, 0U);
            INST(13U, Opcode::Add).s32().Inputs(12U, 1U);
        }

        BASIC_BLOCK(4U, 5U)
        {
            INST(14U, Opcode::Max).s32().Inputs(0U, 1U);
            INST(15U, Opcode::Sub).s32().Inputs(14U, 1U);
            INST(16U, Opcode::Mul).f32().Inputs(5U, 4U);
            INST(17U, Opcode::Mul).f32().Inputs(16U, 5U);
        }
        BASIC_BLOCK(5U, -1L)
        {
            INST(18U, Opcode::And).b().Inputs(1U, 0U);
            INST(19U, Opcode::Xor).b().Inputs(18U, 1U);
            INST(20U, Opcode::ReturnVoid);
        }
    }

    // Delete Insts 12, 14, 16, 18
    Graph *graph_csed = CreateEmptyGraph();
    GRAPH(graph_csed)
    {
        PARAMETER(0U, 0U).s32();
        PARAMETER(1U, 1U).s32();
        PARAMETER(2U, 2U).f64();
        PARAMETER(3U, 3U).f64();
        PARAMETER(4U, 4U).f32();
        PARAMETER(5U, 5U).f32();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(6U, Opcode::Add).s32().Inputs(0U, 1U);
            INST(7U, Opcode::Max).s32().Inputs(1U, 0U);
            INST(8U, Opcode::Mul).f32().Inputs(4U, 5U);
            INST(9U, Opcode::And).b().Inputs(0U, 1U);
            INST(35U, Opcode::SaveState).NoVregs();
            INST(21U, Opcode::CallStatic).b().InputsAutoType(6U, 7U, 8U, 9U, 35U);
            INST(10U, Opcode::Compare).b().SrcType(DataType::Type::INT32).Inputs(0U, 1U);
            INST(11U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(10U);
        }
        BASIC_BLOCK(3U, 5U)
        {
            INST(13U, Opcode::Add).s32().Inputs(6U, 1U);
        }

        BASIC_BLOCK(4U, 5U)
        {
            INST(15U, Opcode::Sub).s32().Inputs(7U, 1U);
            INST(17U, Opcode::Mul).f32().Inputs(8U, 5U);
        }
        BASIC_BLOCK(5U, -1L)
        {
            INST(19U, Opcode::Xor).b().Inputs(9U, 1U);
            INST(20U, Opcode::ReturnVoid);
        }
    }

    ASSERT_TRUE(GetGraph()->RunPass<Cse>());
    GraphChecker(GetGraph()).Check();
    ASSERT_TRUE(GraphComparator().Compare(GetGraph(), graph_csed));
}

TEST_F(CSETest, CSETestApply6)
{
    // use Phi to replace the arithmetic instruction which has appeared in the two preds of its block (commutative).
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();
        PARAMETER(3U, 3U).u64();
        PARAMETER(4U, 4U).b();
        PARAMETER(5U, 5U).b();
        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(6U, Opcode::Compare).b().SrcType(DataType::Type::UINT64).Inputs(0U, 1U);
            INST(7U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(6U);
        }
        BASIC_BLOCK(3U, 5U)
        {
            INST(8U, Opcode::Or).b().Inputs(0U, 1U);
            INST(9U, Opcode::Min).u64().Inputs(1U, 0U);
            INST(10U, Opcode::Xor).b().Inputs(4U, 5U);
            INST(11U, Opcode::Max).u64().Inputs(3U, 2U);

            INST(22U, Opcode::Or).b().Inputs(8U, 1U);
            INST(23U, Opcode::Min).u64().Inputs(9U, 0U);
            INST(24U, Opcode::Xor).b().Inputs(10U, 5U);
            INST(25U, Opcode::Max).u64().Inputs(11U, 2U);
        }
        BASIC_BLOCK(4U, 5U)
        {
            INST(12U, Opcode::Or).b().Inputs(1U, 0U);
            INST(13U, Opcode::Min).u64().Inputs(0U, 1U);
            INST(14U, Opcode::Xor).b().Inputs(5U, 4U);
            INST(15U, Opcode::Max).u64().Inputs(2U, 3U);

            INST(26U, Opcode::Or).b().Inputs(12U, 1U);
            INST(27U, Opcode::Min).u64().Inputs(13U, 0U);
            INST(28U, Opcode::Xor).b().Inputs(14U, 5U);
            INST(29U, Opcode::Max).u64().Inputs(15U, 2U);
        }
        BASIC_BLOCK(5U, -1L)
        {
            INST(16U, Opcode::Or).b().Inputs(1U, 0U);
            INST(17U, Opcode::Min).u64().Inputs(0U, 1U);
            INST(18U, Opcode::Xor).b().Inputs(5U, 4U);
            INST(19U, Opcode::Max).u64().Inputs(2U, 3U);
            INST(35U, Opcode::SaveState).NoVregs();
            INST(20U, Opcode::CallStatic).b().InputsAutoType(16U, 17U, 18U, 19U, 35U);
            INST(21U, Opcode::ReturnVoid);
        }
    }

    // Add Phi 30, 31, 32, 33; Delete Inst 16, 17, 18, 19
    Graph *graph_csed = CreateEmptyGraph();
    GRAPH(graph_csed)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).u64();
        PARAMETER(3U, 3U).u64();
        PARAMETER(4U, 4U).b();
        PARAMETER(5U, 5U).b();
        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(6U, Opcode::Compare).b().SrcType(DataType::Type::UINT64).Inputs(0U, 1U);
            INST(7U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0U).Inputs(6U);
        }
        BASIC_BLOCK(3U, 5U)
        {
            INST(8U, Opcode::Or).b().Inputs(0U, 1U);
            INST(9U, Opcode::Min).u64().Inputs(1U, 0U);
            INST(10U, Opcode::Xor).b().Inputs(4U, 5U);
            INST(11U, Opcode::Max).u64().Inputs(3U, 2U);

            INST(22U, Opcode::Or).b().Inputs(8U, 1U);
            INST(23U, Opcode::Min).u64().Inputs(9U, 0U);
            INST(24U, Opcode::Xor).b().Inputs(10U, 5U);
            INST(25U, Opcode::Max).u64().Inputs(11U, 2U);
        }
        BASIC_BLOCK(4U, 5U)
        {
            INST(12U, Opcode::Or).b().Inputs(1U, 0U);
            INST(13U, Opcode::Min).u64().Inputs(0U, 1U);
            INST(14U, Opcode::Xor).b().Inputs(5U, 4U);
            INST(15U, Opcode::Max).u64().Inputs(2U, 3U);

            INST(26U, Opcode::Or).b().Inputs(12U, 1U);
            INST(27U, Opcode::Min).u64().Inputs(13U, 0U);
            INST(28U, Opcode::Xor).b().Inputs(14U, 5U);
            INST(29U, Opcode::Max).u64().Inputs(15U, 2U);
        }
        BASIC_BLOCK(5U, -1L)
        {
            INST(30U, Opcode::Phi).b().Inputs({{3U, 8U}, {4U, 12U}});
            INST(31U, Opcode::Phi).u64().Inputs({{3U, 9U}, {4U, 13U}});
            INST(32U, Opcode::Phi).b().Inputs({{3U, 10U}, {4U, 14U}});
            INST(33U, Opcode::Phi).u64().Inputs({{3U, 11U}, {4U, 15U}});
            INST(35U, Opcode::SaveState).NoVregs();
            INST(20U, Opcode::CallStatic).b().InputsAutoType(30U, 31U, 32U, 33U, 35U);
            INST(21U, Opcode::ReturnVoid);
        }
    }

    ASSERT_TRUE(GetGraph()->RunPass<Cse>());
    GraphChecker(GetGraph()).Check();
    ASSERT_TRUE(GraphComparator().Compare(GetGraph(), graph_csed));
}

TEST_F(CSETest, CSETestNotApply1)
{
    // Instructions has different type, inputs, opcodes
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).f64();
        PARAMETER(3U, 3U).f64();
        PARAMETER(4U, 4U).f32();
        PARAMETER(5U, 5U).f32();
        PARAMETER(6U, 6U).f32();

        BASIC_BLOCK(2U, -1L)
        {
            INST(7U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(8U, Opcode::Sub).u32().Inputs(1U, 0U);
            INST(9U, Opcode::Mul).f32().Inputs(4U, 5U);

            INST(10U, Opcode::Sub).u64().Inputs(0U, 1U);  // different opcode
            INST(11U, Opcode::Sub).u64().Inputs(1U, 0U);  // different type
            INST(12U, Opcode::Mul).f32().Inputs(4U, 6U);  // different inputs
            INST(35U, Opcode::SaveState).NoVregs();
            INST(13U, Opcode::CallStatic).b().InputsAutoType(7U, 8U, 9U, 10U, 11U, 12U, 35U);
            INST(14U, Opcode::ReturnVoid);
        }
    }
    // graph does not change
    Graph *graph_csed = CreateEmptyGraph();
    GRAPH(graph_csed)
    {
        PARAMETER(0U, 0U).u64();
        PARAMETER(1U, 1U).u64();
        PARAMETER(2U, 2U).f64();
        PARAMETER(3U, 3U).f64();
        PARAMETER(4U, 4U).f32();
        PARAMETER(5U, 5U).f32();
        PARAMETER(6U, 6U).f32();

        BASIC_BLOCK(2U, -1L)
        {
            INST(7U, Opcode::Add).u64().Inputs(0U, 1U);
            INST(8U, Opcode::Sub).u32().Inputs(1U, 0U);
            INST(9U, Opcode::Mul).f32().Inputs(4U, 5U);

            INST(10U, Opcode::Sub).u64().Inputs(0U, 1U);
            INST(11U, Opcode::Sub).u64().Inputs(1U, 0U);
            INST(12U, Opcode::Mul).f32().Inputs(4U, 6U);
            INST(35U, Opcode::SaveState).NoVregs();
            INST(13U, Opcode::CallStatic).b().InputsAutoType(7U, 8U, 9U, 10U, 11U, 12U, 35U);
            INST(14U, Opcode::ReturnVoid);
        }
    }

    GraphChecker(GetGraph()).Check();
    ASSERT_FALSE(GetGraph()->RunPass<Cse>());
    ASSERT_TRUE(GraphComparator().Compare(GetGraph(), graph_csed));
}

TEST_F(CSETest, CSETestNotApply2)
{
    // Instructions in different blocks between which there is no dominating relationship.
    GRAPH(GetGraph())
    {
        PARAMETER(0U, 0U).s32();
        PARAMETER(1U, 1U).s32();
        PARAMETER(2U, 2U).s32();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(3U, Opcode::Compare).b().SrcType(DataType::Type::INT32).Inputs(0U, 1U);
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_GE).Imm(0U).Inputs(3U);
        }

        BASIC_BLOCK(4U, 5U)
        {
            INST(5U, Opcode::Add).s32().Inputs(1U, 0U);
        }

        BASIC_BLOCK(3U, 5U, 6U)
        {
            INST(6U, Opcode::Compare).b().SrcType(DataType::Type::INT32).Inputs(1U, 0U);
            INST(7U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_GE).Imm(0U).Inputs(6U);
        }

        BASIC_BLOCK(6U, 5U)
        {
            INST(8U, Opcode::Add).s32().Inputs(1U, 0U);
        }

        BASIC_BLOCK(5U, -1L)
        {
            INST(9U, Opcode::Phi).s32().Inputs({{4U, 5U}, {3U, 2U}, {6U, 8U}});
            INST(10U, Opcode::Return).s32().Inputs(9U);
        }
    }

    // Graph does not change
    Graph *graph_csed = CreateEmptyGraph();
    GRAPH(graph_csed)
    {
        PARAMETER(0U, 0U).s32();
        PARAMETER(1U, 1U).s32();
        PARAMETER(2U, 2U).s32();

        BASIC_BLOCK(2U, 3U, 4U)
        {
            INST(3U, Opcode::Compare).b().SrcType(DataType::Type::INT32).Inputs(0U, 1U);
            INST(4U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_GE).Imm(0U).Inputs(3U);
        }

        BASIC_BLOCK(4U, 5U)
        {
            INST(5U, Opcode::Add).s32().Inputs(1U, 0U);
        }

        BASIC_BLOCK(3U, 5U, 6U)
        {
            INST(6U, Opcode::Compare).b().SrcType(DataType::Type::INT32).Inputs(1U, 0U);
            INST(7U, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_GE).Imm(0U).Inputs(6U);
        }

        BASIC_BLOCK(6U, 5U)
        {
            INST(8U, Opcode::Add).s32().Inputs(1U, 0U);
        }

        BASIC_BLOCK(5U, -1L)
        {
            INST(9U, Opcode::Phi).s32().Inputs({{4U, 5U}, {3U, 2U}, {6U, 8U}});
            INST(10U, Opcode::Return).s32().Inputs(9U);
        }
    }

    GraphChecker(GetGraph()).Check();
    ASSERT_FALSE(GetGraph()->RunPass<Cse>());
    ASSERT_TRUE(GraphComparator().Compare(GetGraph(), graph_csed));
}
// NOLINTEND(readability-magic-numbers)

}  // namespace panda::compiler
